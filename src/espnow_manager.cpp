#include <cstring>

#include "esp_log.h"
#include "esp_attr.h"

#include "discovery_manager.hpp"
#include "heartbeat_manager.hpp"
#include "message_codec.hpp"
#include "message_router.hpp"
#include "node_state_machine.hpp"
#include "pairing_manager.hpp"
#include "peer_manager.hpp"
#include "protocol_messages.hpp"
#include "hal_timer.hpp"
#include "tx_manager.hpp"
#include "tx_state_machine.hpp"
#include "hal_wifi.hpp"
#include "espnow_driver.hpp"
#include "hal_freertos.hpp"
#include "hal_nvs.hpp"
#include "persistence_backend.hpp"
#include "storage_manager.hpp"
#include "channel_monitor.hpp"

#include "espnow_manager.hpp"

static const char* TAG = "EspNowManager";

// RTC storage for peer list persistence must stay in global scope
static RTC_DATA_ATTR PersistentPeers g_rtc_peers;
static RTC_DATA_ATTR PersistentChannel g_rtc_channel;

// Singleton Factory Constructor ---- (not used in host based tests) LCOV_EXCL_START
EspNowManager& EspNowManager::instance()
{
    static NvsHAL nvs_hal;
    static auto rtc_peers_backend = std::make_unique<RtcBackend>(&g_rtc_peers, sizeof(g_rtc_peers));
    static auto rtc_channel_backend = std::make_unique<RtcBackend>(&g_rtc_channel, sizeof(g_rtc_channel));
    static auto nvs_peers_backend = std::make_unique<NvsBackend>(nvs_hal, "peers_data");
    static auto nvs_channel_backend = std::make_unique<NvsBackend>(nvs_hal, "channel_data");
    static auto storage = std::make_unique<StorageManager>(
        std::move(rtc_peers_backend),
        std::move(rtc_channel_backend),
        std::move(nvs_peers_backend),
        std::move(nvs_channel_backend));

    static auto hal_wifi = std::make_unique<WiFiHAL>();
    static auto hal_timer = std::make_unique<TimerHAL>();
    static auto hal_freertos = std::make_unique<FreeRTOSHAL>();
    static auto bootstraper = std::make_unique<EspNowDriver>(*hal_wifi);
    static auto peer_manager = std::make_unique<PeerManager>(*storage, *hal_wifi, *hal_freertos);
    static auto message_codec = std::make_unique<MessageCodec>();
    static auto channel_monitor = std::make_unique<ChannelMonitor>(*hal_wifi, *hal_freertos);
    static auto scanner = std::make_unique<DiscoveryManager>(*hal_wifi, *message_codec, *hal_freertos);
    static auto tx_fsm = std::make_unique<TxStateMachine>();
    static auto tx_manager = std::make_unique<TxManager>(*tx_fsm, *hal_wifi, *hal_freertos, *message_codec, 500);
    static auto heartbeat_mgr = std::make_unique<HeartbeatManager>(*tx_manager, *peer_manager, *hal_timer);
    static auto pairing_mgr = std::make_unique<PairingManager>(*tx_manager, *peer_manager, *hal_freertos);
    static auto message_router = std::make_unique<MessageRouter>(*scanner, *tx_manager, *heartbeat_mgr, *pairing_mgr);

    static EspNowManager instance(
        std::move(storage),
        std::move(hal_wifi),
        std::move(hal_timer),
        std::move(hal_freertos),
        std::move(bootstraper),
        std::move(peer_manager),
        std::move(message_codec),
        std::move(channel_monitor),
        std::move(scanner),
        std::move(tx_fsm),
        std::move(tx_manager),
        std::move(heartbeat_mgr),
        std::move(pairing_mgr),
        std::move(message_router),
        std::make_unique<NodeStateMachine>());
    return instance;
}
// LCOV_EXCL_STOP

EspNowManager::EspNowManager(
    std::unique_ptr<IStorageManager> storage,
    std::unique_ptr<IWiFiHAL> hal_wifi,
    std::unique_ptr<ITimerHAL> hal_timer,
    std::unique_ptr<IFreeRTOSHAL> hal_freertos,
    std::unique_ptr<IEspNowDriver> espnow_driver,
    std::unique_ptr<IPeerManager> peer_manager,
    std::unique_ptr<IMessageCodec> message_codec,
    std::unique_ptr<IChannelMonitor> channel_monitor,
    std::unique_ptr<IDiscoveryManager> scanner,
    std::unique_ptr<ITxStateMachine> tx_fsm,
    std::unique_ptr<ITxManager> tx_manager,
    std::unique_ptr<IHeartbeatManager> heartbeat_manager,
    std::unique_ptr<IPairingManager> pairing_manager,
    std::unique_ptr<IMessageRouter> message_router,
    std::unique_ptr<INodeStateMachine> node_fsm)
    : storage_(std::move(storage))
    , hal_wifi_(std::move(hal_wifi))
    , hal_timer_(std::move(hal_timer))
    , hal_freertos_(std::move(hal_freertos))
    , espnow_driver_(std::move(espnow_driver))
    , peer_manager_(std::move(peer_manager))
    , message_codec_(std::move(message_codec))
    , channel_monitor_(std::move(channel_monitor))
    , scanner_(std::move(scanner))
    , tx_fsm_(std::move(tx_fsm))
    , tx_manager_(std::move(tx_manager))
    , heartbeat_manager_(std::move(heartbeat_manager))
    , pairing_manager_(std::move(pairing_manager))
    , message_router_(std::move(message_router))
    , node_fsm_(std::move(node_fsm))
{
}

EspNowManager::~EspNowManager()
{
    // Caller is responsible for calling deinit() before destruction.
    // Resources allocated in init() must be explicitly released.
}

// =========================================================================================
// Public API
// =========================================================================================

esp_err_t EspNowManager::init(const EspNowConfig& config)
{
    if (node_fsm_->get_state() != NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config.app_rx_queue == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    config_ = config;
    esp_err_t ret = ESP_OK;

    // Storage needs to be initialized to load channel from storage before EspNowDriver
    if (storage_ != nullptr) {
        uint8_t stored_channel;
        if (storage_->load_channel(stored_channel) == ESP_OK) {
            config_.wifi_channel = stored_channel;
        }
    }

    // EspNowDriver initializes ESPNOW
    ret = espnow_driver_->init(config_, esp_now_recv_cb, esp_now_send_cb);
    if (ret != ESP_OK) {
        return init_fail(ret, "espnow_driver");
    }
    esp_now_initialized_ = true;

    ret = create_mutex();
    if (ret != ESP_OK) {
        return init_fail(ret, "mutex");
    }

    ret = create_queue();
    if (ret != ESP_OK) {
        return init_fail(ret, "queues");
    }

    ret = create_task();
    if (ret != ESP_OK) {
        return init_fail(ret, "tasks");
    }

    ret = init_tx_manager();
    if (ret != ESP_OK) {
        return init_fail(ret, "tx_manager");
    }

    ret = init_discovery_manager();
    if (ret != ESP_OK) {
        return init_fail(ret, "discovery_manager");
    }

    ret = init_heartbeat_manager();
    if (ret != ESP_OK) {
        return init_fail(ret, "heartbeat_manager");
    }

    ret = init_pairing_manager();
    if (ret != ESP_OK) {
        return init_fail(ret, "pairing_manager");
    }

    ret = init_channel_monitor();
    if (ret != ESP_OK) {
        return init_fail(ret, "channel_monitor");
    }

    // Load peers from storage and add them to ESP-NOW
    peer_manager_->load_peers_from_storage();
    etl::vector<PeerInfo, MAX_PEERS> peers = peer_manager_->get_all();

    // NodeStateMachine decides the initial state based on peers presence
    NodeState old_state = node_fsm_->get_state();
    node_fsm_->on_init(!peers.empty());
    NodeState new_state = node_fsm_->get_state();

    if (new_state == NodeState::OPERATIONAL) {
        add_peers_to_espnow(peers);
    }

    // React to the initial state transition
    handle_state_transition(old_state, new_state);

    // Update scanner and storage with current channel
    scanner_->set_channel(config_.wifi_channel);
    storage_->store_channel(config_.wifi_channel);

    ESP_LOGI(TAG, "EspNow component initialized successfully.");
    return ret;
}

void EspNowManager::deinit()
{
    ESP_LOGI(TAG, "Deinitializing EspNowManager...");

    if (tx_manager_ != nullptr) {
        tx_manager_->deinit();
    }
    if (heartbeat_manager_ != nullptr) {
        // Deinit no more exists because heartbeat_manager_ uses tick() instead of freeRTOS timers
        // heartbeat_manager_->deinit();
    }

    if (rx_task_handle_ != nullptr) {
        // Signal rx task to stop
        signal_task_to_stop();
        // Delete tasks if not terminated gracefully
        delete_task();
    }

    // Call cleanup_resources to delete queues and mutex
    if (rx_queue_handle_ != nullptr || ack_mutex_ != nullptr) {
        cleanup_resources();
    }

    // Delete peers
    if (esp_now_initialized_ && peer_manager_) {
        etl::vector<PeerInfo, MAX_PEERS> peers = peer_manager_->get_all();
        for (const auto& peer : peers) {
            hal_wifi_->hal_esp_now_del_peer(peer.mac);
        }
    }

    // Call EspNowDriver to deinit ESP-NOW
    if (espnow_driver_ != nullptr) {
        espnow_driver_->deinit();
    }

    if (scanner_ != nullptr) {
        scanner_->deinit();
    }

    // Reset state
    esp_now_initialized_ = false;
    last_header_requiring_ack_.reset();
    config_ = EspNowConfig();
    node_fsm_->on_deinit();
    ESP_LOGI(TAG, "EspNow component deinitialized.");
}

esp_err_t EspNowManager::start_pairing(uint32_t timeout_ms)
{
    // TODO: on_pairing_requested already check if node is uninitialized,
    // acepts only IDLE or OPERATIONAL state
    if (node_fsm_->get_state() == NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    // Store timeout for use when pairing start or scan completes
    pairing_timeout_ms_ = timeout_ms;

    NodeState old_state = node_fsm_->get_state();
    bool has_peers = !peer_manager_->get_all().empty();
    node_fsm_->on_pairing_requested(has_peers);
    NodeState new_state = node_fsm_->get_state();

    //
    if (old_state != new_state) {
        handle_state_transition(old_state, new_state);
    }

    return ESP_OK;
}

esp_err_t EspNowManager::send_data(
    NodeId dest_node_id,
    PayloadType payload_type,
    const void* payload,
    size_t len,
    bool require_ack)
{
    return send_packet(dest_node_id, MessageType::DATA, payload_type, payload, len, require_ack);
}

esp_err_t EspNowManager::send_command(
    NodeId dest_node_id,
    CommandType command_type,
    const void* payload,
    size_t len,
    bool require_ack)
{
    return send_packet(
        dest_node_id, MessageType::COMMAND, static_cast<PayloadType>(command_type), payload, len, require_ack);
}

esp_err_t EspNowManager::confirm_reception(AckStatus status)
{
    if (node_fsm_->get_state() != NodeState::OPERATIONAL)
        return ESP_ERR_INVALID_STATE;

    if (hal_freertos_->semaphore_take(ack_mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    if (!last_header_requiring_ack_.has_value()) {
        hal_freertos_->semaphore_give(ack_mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    const auto& header_to_ack = last_header_requiring_ack_.value();

    DecodedTxPacket tx_packet;
    if (!peer_manager_->find_mac(header_to_ack.sender_node_id, tx_packet.dest_mac)) {
        last_header_requiring_ack_.reset();
        hal_freertos_->semaphore_give(ack_mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    tx_packet.header.msg_type = MessageType::ACK;
    tx_packet.header.sender_node_id = config_.node_id;
    tx_packet.header.sender_type = config_.node_type;
    tx_packet.header.dest_node_id = header_to_ack.sender_node_id;
    tx_packet.header.sequence_number = 0;
    tx_packet.header.requires_ack = false;
    tx_packet.header.payload_type = 0;
    tx_packet.header.timestamp_ms = get_time_ms();

    // Payload for ACK is [ack_sequence (2 bytes) + status (1 byte)]
    tx_packet.payload_len = 3;
    uint16_t ack_seq = header_to_ack.sequence_number;
    memcpy(tx_packet.payload, &ack_seq, 2);
    tx_packet.payload[2] = static_cast<uint8_t>(status);

    esp_err_t err = tx_manager_->queue_packet(tx_packet);
    last_header_requiring_ack_.reset();
    hal_freertos_->semaphore_give(ack_mutex_);
    return err;
}

// Peer management
esp_err_t EspNowManager::add_peer(NodeId node_id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms)
{
    return peer_manager_->add(node_id, mac, type, heartbeat_interval_ms);
}

esp_err_t EspNowManager::remove_peer(NodeId node_id)
{
    return peer_manager_->remove(node_id);
}

etl::vector<NodeId, MAX_PEERS> EspNowManager::get_offline_peers() const
{
    if (node_fsm_->get_state() != NodeState::OPERATIONAL)
        return {};
    return peer_manager_->get_offline(get_time_ms());
}

// Getters
NodeState EspNowManager::get_node_state() const
{
    return node_fsm_->get_state();
}

bool EspNowManager::is_initialized() const
{
    return node_fsm_->get_state() != NodeState::UNINITIALIZED;
}

etl::vector<PeerInfo, MAX_PEERS> EspNowManager::get_peers()
{
    return peer_manager_->get_all();
}

// =========================================================================================
// ESP-NOW callbacks - called by ESP-NOW driver in ISR context --- LCOV_EXCL_START
// =========================================================================================
void EspNowManager::esp_now_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
    if (!info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN)
        return;
    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len = len;
    packet.rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
    packet.timestamp_us = instance().hal_timer_->get_time_us();
    instance().hal_freertos_->queue_send_fromISR(instance().rx_queue_handle_, &packet, 0);
}

void EspNowManager::esp_now_send_cb(const esp_now_send_info_t* info, esp_now_send_status_t status)
{
    if (info->tx_status == static_cast<wifi_tx_status_t>(ESP_NOW_SEND_FAIL))
        instance().tx_manager_->notify_physical_fail();
}
// LCOV_EXCL_STOP

// =========================================================================================
// Task implementations
// =========================================================================================
void EspNowManager::rx_task(void* arg)
{
    EspNowManager* self = static_cast<EspNowManager*>(arg);

    RxPacket packet{};
    DecodedPacket decoded{};
    uint32_t notifications = 0;
    bool should_stop = false;

    while (true) {
        // Check for pending notifications without blocking (timeout = 0). We prioritize packet reception below
        // notifications are checked every ~100 ms as a side effect of the queue_receive timeout.
        if (self->hal_freertos_->task_notify_wait(0, 0xFFFFFFFF, &notifications, 0) == pdTRUE) {
            // Check for notifications and handle them
            self->handle_notifications(notifications, should_stop);
            // If stop is requested, break the loop
            if (should_stop) {
                break;
            }
        }

        // Wait for incoming packets with a timeout to periodically check for notifications and tick managers.
        if (self->hal_freertos_->queue_receive(self->rx_queue_handle_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Validate CRC
            if (self->message_codec_->validate_crc(packet.data, packet.len)) {
                // Decode header
                auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
                if (header_opt) {
                    // Any valid packet is proof the link is alive — notify TxManager
                    // regardless of message type, before routing.
                    self->tx_manager_->notify_link_alive();

                    decoded = {packet, header_opt.value()};

                    // Application-level packets — deliver directly to app queue
                    if (decoded.header.msg_type == MessageType::DATA ||
                        decoded.header.msg_type == MessageType::COMMAND) {
                        if (decoded.header.requires_ack) {
                            if (self->hal_freertos_->semaphore_take(self->ack_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
                                self->last_header_requiring_ack_ = decoded.header;
                                self->hal_freertos_->semaphore_give(self->ack_mutex_);
                            }
                        }
                        if (self->config_.app_rx_queue != nullptr) {
                            AppMessage msg = self->build_app_message(decoded);
                            if (self->hal_freertos_->queue_send(self->config_.app_rx_queue, &msg, 0) != pdTRUE) {
                                ESP_LOGW(TAG, "App queue full, dropping packet");
                            }
                        }
                    }
                    else {
                        // Protocol-internal packets — handle immediately via router
                        self->message_router_->handle_packet(decoded);

                        // A NODE ends its pairing time (!is_active) immediately when it receives a PAIR_RESPONSE
                        // when SCANNING is sucessfull. The peer is add to the peer manager so we check if node
                        // has peer and call on_pairing_timeout.
                        // TODO: remove this after full refactor, pairing done is notified to rx_task
                        // if (!self->pairing_manager_->is_active()) {
                        //     bool has_peers = !self->peer_manager_->get_all().empty();
                        //     self->node_fsm_->on_pairing_timeout(has_peers);
                        // }
                    }
                }
            }
        }
        // HUBs go to pairing inactive only by timeout, not when sucessfull pair a node.
        // Non_HUB cannot be handled here because the scanning process is not instantaneous and if
        // on_pairing_timeout is called before the scan is completed, the node will go to IDLE instead of
        // OPERATIONAL.
        // TODO: remove this after full refactor, pairing done is notified to rx_task
        // if (self->config_.node_type == ReservedTypes::HUB && !self->pairing_manager_->is_active()) {
        //     bool has_peers = !self->peer_manager_->get_all().empty();
        //     self->node_fsm_->on_pairing_timeout(has_peers);
        // }

        // Tick submodules to handle timers
        NodeState current_state = self->node_fsm_->get_state();
        if (current_state == NodeState::PAIRING) {
            self->pairing_manager_->tick(self->get_time_ms());
        }
        else if (current_state == NodeState::OPERATIONAL) {
            self->heartbeat_manager_->tick(self->get_time_ms());
        }
        if (current_state != NodeState::UNINITIALIZED) {
            self->channel_monitor_->tick(self->get_time_ms());
        }
    }

    // Task cleanup on exit
    self->rx_task_handle_ = nullptr;
    self->hal_freertos_->task_suspend(NULL);
    self->hal_freertos_->task_delete(NULL);
}

// =========================================================================================
// Internal methods
// =========================================================================================

// Helper method used by send_data and send_command
esp_err_t EspNowManager::send_packet(
    NodeId dest_node_id,
    MessageType msg_type,
    PayloadType payload_type,
    const void* payload,
    size_t len,
    bool require_ack)
{
    if (node_fsm_->get_state() != NodeState::OPERATIONAL)
        return ESP_ERR_INVALID_STATE;

    DecodedTxPacket tx_packet;
    if (!peer_manager_->find_mac(dest_node_id, tx_packet.dest_mac))
        return ESP_ERR_NOT_FOUND;

    tx_packet.header.msg_type = msg_type;
    tx_packet.header.sequence_number = 0;
    tx_packet.header.sender_type = config_.node_type;
    tx_packet.header.sender_node_id = config_.node_id;
    tx_packet.header.payload_type = payload_type;
    tx_packet.header.requires_ack = require_ack;
    tx_packet.header.dest_node_id = dest_node_id;
    tx_packet.header.timestamp_ms = get_time_ms();

    if (len > MAX_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }

    tx_packet.payload_len = len;
    if (payload != nullptr && len > 0) {
        memcpy(tx_packet.payload, payload, len);
    }

    return tx_manager_->queue_packet(tx_packet);
}

uint64_t EspNowManager::get_time_ms() const
{
    return hal_timer_->get_time_us() / 1000;
}

// Helper to build AppMessage from DecodedPacket
AppMessage EspNowManager::build_app_message(const DecodedPacket& decoded)
{
    AppMessage msg{};
    msg.sender_id = decoded.header.sender_node_id;
    msg.sender_type = decoded.header.sender_type;
    msg.payload_type = decoded.header.payload_type;
    msg.requires_ack = decoded.header.requires_ack;
    memcpy(msg.src_mac, decoded.raw.src_mac, 6);

    const size_t payload_offset = sizeof(MessageHeader);
    msg.payload_len = decoded.raw.len - payload_offset - CRC_SIZE;
    memcpy(msg.payload, decoded.raw.data + payload_offset, msg.payload_len);

    return msg;
}

void EspNowManager::handle_notifications(uint32_t notifications, bool& should_stop)
{
    // If we receive NOTIFY_MAX_FAILURES from TxManager::tx_task()
    if ((notifications & NOTIFY_MAX_FAILURES) == NOTIFY_MAX_FAILURES) {
        NodeState old_state = node_fsm_->get_state();
        node_fsm_->on_scan_requested();
        handle_state_transition(old_state, node_fsm_->get_state());
    }

    // NOTIFY_CHANNEL_FOUND is set by DiscoveryManager::discovery_task()
    if ((notifications & NOTIFY_CHANNEL_FOUND) == NOTIFY_CHANNEL_FOUND) {
        // Calls get_channel() to get the channel where the HUB was found
        config_.wifi_channel = scanner_->get_channel();

        NodeState old_state = node_fsm_->get_state();
        node_fsm_->on_channel_found();
        handle_state_transition(old_state, node_fsm_->get_state());
    }

    // NOTIFY_PAIRING_DONE is set by PairingManager::notify_rx_task_pairing_done()
    if ((notifications & NOTIFY_PAIRING_DONE) == NOTIFY_PAIRING_DONE) {
        bool has_peers = !peer_manager_->get_all().empty();

        NodeState old_state = node_fsm_->get_state();
        node_fsm_->on_pairing_timeout(has_peers);
        handle_state_transition(old_state, node_fsm_->get_state());
    }

    // NOTIFY_SCAN_FAILED is set by DiscoveryManager::discovery_task()
    if ((notifications & NOTIFY_SCAN_FAILED) == NOTIFY_SCAN_FAILED) {
        // By now, with or without peers, the node goes to IDLE, but
        // we check here if we change NodeStateMachine later
        bool has_peers = !peer_manager_->get_all().empty();
        NodeState old_state = node_fsm_->get_state();
        node_fsm_->on_scan_failed(has_peers);
        handle_state_transition(old_state, node_fsm_->get_state());
    }

    // If NOTIFY_CHANNEL_CHANGED is set by on_channel_changed_cb()
    if ((notifications & NOTIFY_CHANNEL_CHANGED) == NOTIFY_CHANNEL_CHANGED) {
        uint8_t channel = channel_monitor_->get_wifi_channel();
        config_.wifi_channel = channel;
        scanner_->set_channel(channel);
        storage_->store_channel(channel);
        ESP_LOGI(TAG, "Channel changed to %d", channel);
    }

    // If NOTIFY_STOP is set, we break the loop and exit the task.
    if (notifications & NOTIFY_STOP) {
        should_stop = true;
    }
}

void EspNowManager::handle_state_transition(NodeState old_state, NodeState new_state)
{
    if (old_state == new_state) {
        return;
    }

    ESP_LOGI(TAG, "Reacting to state change: %d -> %d", static_cast<int>(old_state), static_cast<int>(new_state));

    switch (new_state) {
    case NodeState::PAIRING_SCAN:
    case NodeState::RECOVERY_SCAN:
        scanner_->start_scan();
        break;

    case NodeState::PAIRING:
        if (scanner_->is_scanning()) {
            scanner_->stop_scan();
        }
        pairing_manager_->start(pairing_timeout_ms_, get_time_ms());
        break;

    case NodeState::OPERATIONAL:
        if (scanner_->is_scanning()) {
            scanner_->stop_scan();
        }
        // If we just rediscovered the channel, store it
        if (old_state == NodeState::RECOVERY_SCAN || old_state == NodeState::PAIRING_SCAN) {
            storage_->store_channel(config_.wifi_channel);
        }
        break;

    case NodeState::IDLE:
        if (scanner_->is_scanning()) {
            scanner_->stop_scan();
        }
        break;

    default:
        break;
    }
}

// ==================================================================
// Init helpers
// ==================================================================

esp_err_t EspNowManager::create_mutex()
{
    ack_mutex_ = hal_freertos_->mutex_create();
    if (ack_mutex_ == nullptr) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t EspNowManager::create_queue()
{
    rx_queue_handle_ = hal_freertos_->queue_create(config_.rx_queue_length, sizeof(RxPacket));
    if (rx_queue_handle_ == nullptr) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t EspNowManager::create_task()
{
    BaseType_t ret;
    ret = hal_freertos_->task_create(
        rx_task, "rx_task", config_.stack_size_rx_task, this, config_.priority_rx_task, &rx_task_handle_);

    if (ret != pdPASS) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t EspNowManager::init_tx_manager()
{
    if (tx_manager_ == nullptr) {
        return ESP_FAIL;
    }
    return tx_manager_->init(config_.stack_size_tx_task, config_.priority_tx_task, rx_task_handle_);
}

esp_err_t EspNowManager::init_discovery_manager()
{
    if (scanner_ == nullptr) {
        return ESP_FAIL;
    }
    return scanner_->init(
        config_.node_id, config_.node_type, rx_task_handle_, config_.priority_rx_task, config_.stack_size_rx_task);
}

esp_err_t EspNowManager::init_heartbeat_manager()
{
    if (heartbeat_manager_ == nullptr) {
        return ESP_FAIL;
    }
    heartbeat_manager_->init(config_.node_id, config_.node_type, config_.heartbeat_interval_ms);
    return ESP_OK;
}

esp_err_t EspNowManager::init_pairing_manager()
{
    if (pairing_manager_ == nullptr) {
        return ESP_FAIL;
    }
    return pairing_manager_->init(config_.node_id, config_.node_type, rx_task_handle_);
}

esp_err_t EspNowManager::init_channel_monitor()
{
    if (channel_monitor_ == nullptr) {
        return ESP_FAIL;
    }
    return channel_monitor_->init(config_.channel_monitor_interval_ms, rx_task_handle_);
}

esp_err_t EspNowManager::init_fail(esp_err_t ret, const char* step)
{
    ESP_LOGE(TAG, "init failed at %s: %s", step, esp_err_to_name(ret));
    deinit();
    return ret;
}

void EspNowManager::add_peers_to_espnow(etl::ivector<PeerInfo>& peers)
{
    for (auto& peer : peers) {
        esp_now_peer_info_t peer_info = {};
        memcpy(peer_info.peer_addr, peer.mac, 6);
        peer_info.channel = 0;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = false;
        hal_wifi_->hal_esp_now_add_peer(&peer_info);
    }
}

void EspNowManager::signal_task_to_stop()
{
    // Signal tasks to stop
    if (rx_task_handle_ != nullptr) {
        hal_freertos_->task_notify(rx_task_handle_, NOTIFY_STOP, eSetBits);
    }

    // Wait for tasks to exit (up to 1s).
    uint16_t timeout_ms = 1000;
    uint8_t delay_ms = 10;
    while (timeout_ms > 0) {
        if (rx_task_handle_ == nullptr) {
            break;
        }
        hal_freertos_->task_delay(pdMS_TO_TICKS(delay_ms));
        timeout_ms -= delay_ms;
    }

    if (rx_task_handle_ != nullptr) {
        ESP_LOGW(TAG, "Tasks did not terminate gracefully within timeout");
    }
}

void EspNowManager::delete_task()
{
    if (rx_task_handle_ != nullptr) {
        hal_freertos_->task_suspend(rx_task_handle_);
        hal_freertos_->task_delete(rx_task_handle_);
        rx_task_handle_ = nullptr;
    }
}

void EspNowManager::cleanup_resources()
{
    if (rx_queue_handle_ != nullptr) {
        hal_freertos_->queue_delete(rx_queue_handle_);
        rx_queue_handle_ = nullptr;
    }
    if (ack_mutex_ != nullptr) {
        hal_freertos_->semaphore_delete(ack_mutex_);
        ack_mutex_ = nullptr;
    }
}
