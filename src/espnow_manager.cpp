#include <cstring>

#include "esp_log.h"
#include "esp_attr.h"

#include "discovery_manager.hpp"
#include "heartbeat_manager.hpp"
#include "message_codec.hpp"
#include "message_router.hpp"
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

#include "espnow_manager.hpp"

static const char *TAG = "EspNow";

// RTC storage for peer list persistence must stay in global scope
static RTC_DATA_ATTR PersistentData g_rtc_storage;

// Singleton Factory Constructor ---- (not used in host based tests) LCOV_EXCL_START
EspNowManager &EspNowManager::instance()
{
    static NvsHAL nvs_hal;
    static auto rtc_backend = std::make_unique<RtcBackend>(g_rtc_storage);
    static auto nvs_backend = std::make_unique<NvsBackend>(nvs_hal);
    static StorageManager storage(std::move(rtc_backend), std::move(nvs_backend));

    static auto hal_wifi = std::make_unique<WiFiHAL>();
    static auto hal_timer = std::make_unique<TimerHAL>();
    static auto hal_freertos = std::make_unique<FreeRTOSHAL>();
    static auto bootstraper = std::make_unique<EspNowDriver>(*hal_wifi);
    static auto peer_manager = std::make_unique<PeerManager>(storage, *hal_wifi, *hal_freertos);
    static auto message_codec = std::make_unique<MessageCodec>();
    static auto scanner = std::make_unique<DiscoveryManager>(*hal_wifi, *message_codec, *hal_freertos);
    static auto tx_fsm = std::make_unique<TxStateMachine>();
    static auto tx_manager =
        std::make_unique<TxManager>(*tx_fsm, *scanner, *hal_wifi, *hal_freertos, *message_codec, 500);
    static auto heartbeat_mgr = std::make_unique<HeartbeatManager>(
        ReservedIds::HUB, *tx_manager, *peer_manager, *hal_freertos, *hal_timer);
    static auto pairing_mgr = std::make_unique<PairingManager>(*tx_manager, *peer_manager);
    static auto message_router = std::make_unique<MessageRouter>(*scanner, *tx_manager, *heartbeat_mgr, *pairing_mgr);

    static EspNowManager instance(
        std::move(hal_wifi),
        std::move(hal_timer),
        std::move(hal_freertos),
        std::move(bootstraper),
        std::move(peer_manager),
        std::move(message_codec),
        std::move(scanner),
        std::move(tx_fsm),
        std::move(tx_manager),
        std::move(heartbeat_mgr),
        std::move(pairing_mgr),
        std::move(message_router));
    return instance;
}
// LCOV_EXCL_STOP

EspNowManager::EspNowManager(
    std::unique_ptr<IWiFiHAL> driver_hal,
    std::unique_ptr<ITimerHAL> timer_hal,
    std::unique_ptr<IFreeRTOSHAL> freertos_hal,
    std::unique_ptr<IEspNowDriver> bootstraper,
    std::unique_ptr<IPeerManager> peer_manager,
    std::unique_ptr<IMessageCodec> message_codec,
    std::unique_ptr<IDiscoveryManager> scanner,
    std::unique_ptr<ITxStateMachine> tx_fsm,
    std::unique_ptr<ITxManager> tx_manager,
    std::unique_ptr<IHeartbeatManager> heartbeat_manager,
    std::unique_ptr<IPairingManager> pairing_manager,
    std::unique_ptr<IMessageRouter> message_router)
    : hal_wifi_(std::move(driver_hal))
    , hal_timer_(std::move(timer_hal))
    , hal_freertos_(std::move(freertos_hal))
    , espnow_driver_(std::move(bootstraper))
    , peer_manager_(std::move(peer_manager))
    , message_codec_(std::move(message_codec))
    , scanner_(std::move(scanner))
    , tx_fsm_(std::move(tx_fsm))
    , tx_manager_(std::move(tx_manager))
    , heartbeat_manager_(std::move(heartbeat_manager))
    , pairing_manager_(std::move(pairing_manager))
    , message_router_(std::move(message_router))
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

esp_err_t EspNowManager::init(const EspNowConfig &config)
{
    if (node_state_.load() != NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (config.app_rx_queue == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    config_ = config;
    esp_err_t ret = ESP_OK;

    // PeerManager needs to be initialized to load channel from storage before EspNowDriver
    if (peer_manager_ != nullptr) {
        uint8_t stored_channel;
        if (peer_manager_->load_from_storage(stored_channel) == ESP_OK) {
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

    ret = create_queues();
    if (ret != ESP_OK) {
        return init_fail(ret, "queues");
    }

    ret = create_tasks();
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

    etl::vector<PeerInfo, MAX_PEERS> peers = peer_manager_->get_all();
    NodeState initial_state = determine_initial_state(peers);
    transition_to_state(initial_state);

    if (initial_state == NodeState::OPERATIONAL) {
        add_peers_to_espnow(peers);
    }

    // Update sub componentes with current channel from loaded NVS or Config default
    propagate_channel();

    ESP_LOGI(TAG, "EspNow component initialized successfully.");
    return ret;
}

esp_err_t EspNowManager::deinit()
{
    ESP_LOGI(TAG, "Deinitializing EspNowManager...");

    if (tx_manager_ != nullptr) {
        tx_manager_->deinit();
    }
    if (heartbeat_manager_ != nullptr) {
        heartbeat_manager_->deinit();
    }

    if (rx_task_handle_ != nullptr) {
        // Signal rx task to stop
        signal_tasks_to_stop();
        // Delete tasks if not terminated gracefully
        delete_tasks();
    }

    // Call cleanup_resources to delete queues and mutex
    if (rx_dispatch_queue_ != nullptr || ack_mutex_ != nullptr) {
        cleanup_resources();
    }

    // Delete peers
    if (esp_now_initialized_ && peer_manager_) {
        etl::vector<PeerInfo, MAX_PEERS> peers = peer_manager_->get_all();
        for (const auto &peer : peers) {
            hal_wifi_->hal_esp_now_del_peer(peer.mac);
        }
    }

    // Call EspNowDriver to deinit ESP-NOW
    if (espnow_driver_ != nullptr) {
        espnow_driver_->deinit();
    }

    // Reset state
    esp_now_initialized_ = false;
    last_header_requiring_ack_.reset();
    config_ = EspNowConfig();
    transition_to_state(NodeState::UNINITIALIZED);

    ESP_LOGI(TAG, "EspNow component deinitialized.");
    return ESP_OK;
}

esp_err_t EspNowManager::start_pairing(uint32_t timeout_ms)
{
    if (node_state_.load() == NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    // Store timeout for use when scan completes (NOTIFY_CHANNEL_FOUND or NOTIFY_SCAN_FAILED)
    // pairing_manager_->start()  is called from rx_task after the correct channel is confirmed.
    pairing_timeout_ms_ = timeout_ms;

    // Force TxManager into scanning so the correct channel is discovered
    // before pair requests are sent. Pairing starts after scan result arrives.
    tx_manager_->notify_scanning();

    transition_to_state(NodeState::PAIRING);
    return ESP_OK;
}

esp_err_t EspNowManager::send_data(
    NodeId dest_node_id,
    PayloadType payload_type,
    const void *payload,
    size_t len,
    bool require_ack)
{
    return send_packet(dest_node_id, MessageType::DATA, payload_type, payload, len, require_ack);
}

esp_err_t EspNowManager::send_command(
    NodeId dest_node_id,
    CommandType command_type,
    const void *payload,
    size_t len,
    bool require_ack)
{
    return send_packet(
        dest_node_id, MessageType::COMMAND, static_cast<PayloadType>(command_type), payload, len, require_ack);
}

esp_err_t EspNowManager::confirm_reception(AckStatus status)
{
    if (node_state_.load() != NodeState::OPERATIONAL)
        return ESP_ERR_INVALID_STATE;

    if (hal_freertos_->semaphore_take(ack_mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    if (!last_header_requiring_ack_.has_value()) {
        hal_freertos_->semaphore_give(ack_mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    const auto &header_to_ack = last_header_requiring_ack_.value();
    
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
esp_err_t EspNowManager::add_peer(NodeId node_id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms)
{
    return peer_manager_->add(node_id, mac, type, heartbeat_interval_ms);
}

esp_err_t EspNowManager::remove_peer(NodeId node_id)
{
    return peer_manager_->remove(node_id);
}

etl::vector<NodeId, MAX_PEERS> EspNowManager::get_offline_peers() const
{
    if (node_state_.load() != NodeState::OPERATIONAL)
        return {};
    return peer_manager_->get_offline(get_time_ms());
}

// Getters
NodeState EspNowManager::get_node_state() const
{
    return node_state_.load();
}

bool EspNowManager::is_initialized() const
{
    return node_state_.load() != NodeState::UNINITIALIZED;
}

etl::vector<PeerInfo, MAX_PEERS> EspNowManager::get_peers()
{
    return peer_manager_->get_all();
}

// =========================================================================================
// ESP-NOW callbacks - called by ESP-NOW driver in ISR context --- LCOV_EXCL_START
// =========================================================================================
void EspNowManager::esp_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN)
        return;
    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len = len;
    packet.rssi = static_cast<int8_t>(info->rx_ctrl->rssi);
    packet.timestamp_us = instance().hal_timer_->get_time_us();
    instance().hal_freertos_->queue_send_fromISR(instance().rx_dispatch_queue_, &packet, 0);
}

void EspNowManager::esp_now_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    if (info->tx_status == static_cast<wifi_tx_status_t>(ESP_NOW_SEND_FAIL))
        instance().tx_manager_->notify_physical_fail();
}
// LCOV_EXCL_STOP

// =========================================================================================
// Task implementations
// =========================================================================================
void EspNowManager::rx_task(void *arg)
{
    EspNowManager *self = static_cast<EspNowManager *>(arg);

    RxPacket packet{};
    DecodedPacket decoded{};

    while (true) {
        uint32_t notifications = 0;

        // Check for pending notifications without blocking (timeout = 0). We prioritize packet reception below
        // notifications are checked every ~100 ms as a side effect of the queue_receive timeout.
        if (self->hal_freertos_->task_notify_wait(0, 0xFFFFFFFF, &notifications, 0) == pdTRUE) {
            // Entered scanning state — TxManager is actively searching for the HUB.
            if ((notifications & NOTIFY_SCANNING) == NOTIFY_SCANNING) {
                self->transition_to_state(NodeState::SCANNING);
            }
            // NOTIFY_CHANNEL_FOUND is set by on_channel_found_cb()
            if ((notifications & NOTIFY_CHANNEL_FOUND) == NOTIFY_CHANNEL_FOUND) {
                uint8_t channel = self->last_found_channel_.load();
                self->config_.wifi_channel = channel;
                self->propagate_channel();
                self->peer_manager_->persist();
                // Channel confirmed now safe to start pairing if NodeState::PAIRING
                if (self->node_state_.load() == NodeState::PAIRING)
                    self->pairing_manager_->start(self->pairing_timeout_ms_, self->get_time_ms());
                self->transition_to_state(NodeState::OPERATIONAL);
            }
            // NOTIFY_SCAN_FAILED is set by on_scan_failed_cb()
            if ((notifications & NOTIFY_SCAN_FAILED) == NOTIFY_SCAN_FAILED) {
                self->transition_to_state(NodeState::PAIRING);
            }
            // If NOTIFY_STOP is set, we break the loop and exit the task.
            if (notifications & NOTIFY_STOP) {
                break;
            }
        }

        // Wait for incoming packets with a timeout to periodically check for notifications and tick managers.
        if (self->hal_freertos_->queue_receive(self->rx_dispatch_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Validate CRC
            if (self->message_codec_->validate_crc(packet.data, packet.len)) {
                // Decode header
                auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
                if (header_opt) {
                    decoded = {packet, header_opt.value()};

                    // Application-level packets — deliver directly to app queue
                    if (decoded.header.msg_type == MessageType::DATA || decoded.header.msg_type == MessageType::COMMAND) {
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
                    }
                }
            }
        }

        // Tick pairing manager to handle timeouts
        if (self->node_state_.load() == NodeState::PAIRING) {
            self->pairing_manager_->tick(self->get_time_ms());
        }
    }

    // Task cleanup on exit
    self->rx_task_handle_ = nullptr;
    self->hal_freertos_->task_suspend(NULL);
    self->hal_freertos_->task_delete(NULL);
}

// =========================================================================================
// IChannelObserver implementation for DiscoveryManager callbacks
// =========================================================================================

void EspNowManager::on_channel_found_cb(uint8_t channel)
{
    last_found_channel_.store(channel);
    hal_freertos_->task_notify(rx_task_handle_, NOTIFY_CHANNEL_FOUND, eSetBits);
}

void EspNowManager::on_scan_failed_cb()
{
    hal_freertos_->task_notify(rx_task_handle_, NOTIFY_SCAN_FAILED, eSetBits);
}

void EspNowManager::on_scan_started_cb()
{
    hal_freertos_->task_notify(rx_task_handle_, NOTIFY_SCANNING, eSetBits);
}

// =========================================================================================
// Internal methods
// =========================================================================================

// Helper method used by send_data and send_command
esp_err_t EspNowManager::send_packet(
    NodeId dest_node_id,
    MessageType msg_type,
    PayloadType payload_type,
    const void *payload,
    size_t len,
    bool require_ack)
{
    if (node_state_.load() != NodeState::OPERATIONAL)
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

void EspNowManager::propagate_channel()
{
    heartbeat_manager_->set_channel(config_.wifi_channel);
    pairing_manager_->set_channel(config_.wifi_channel);
    scanner_->set_channel(config_.wifi_channel);
    peer_manager_->set_channel(config_.wifi_channel);
}

void EspNowManager::transition_to_state(NodeState new_state)
{
    ESP_LOGI(TAG, "NodeState: %d -> %d", static_cast<int>(node_state_.load()), static_cast<int>(new_state));
    node_state_.store(new_state);
}

bool EspNowManager::is_protocol_message(MessageType type)
{
    switch (type) {
    case MessageType::PAIR_REQUEST:
    case MessageType::PAIR_RESPONSE:
    case MessageType::HEARTBEAT:
    case MessageType::HEARTBEAT_RESPONSE:
    case MessageType::ACK:
    case MessageType::CHANNEL_SCAN_PROBE:
    case MessageType::CHANNEL_SCAN_RESPONSE:
        return true;
    default:
        return false;
    }
}

// Helper to build AppMessage from DecodedPacket
AppMessage EspNowManager::build_app_message(const DecodedPacket &decoded)
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

esp_err_t EspNowManager::create_queues()
{
    rx_dispatch_queue_ = hal_freertos_->queue_create(config_.rx_queue_length, sizeof(RxPacket));
    if (rx_dispatch_queue_ == nullptr) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t EspNowManager::create_tasks()
{
    BaseType_t ret;
    ret = hal_freertos_->task_create(
        rx_task,
        "rx_task",
        config_.stack_size_rx_task,
        this,
        config_.priority_rx_task,
        &rx_task_handle_);

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
    return tx_manager_->init(config_.stack_size_tx_task, config_.priority_tx_task);
}

esp_err_t EspNowManager::init_discovery_manager()
{
    if (scanner_ == nullptr) {
        return ESP_FAIL;
    }
    esp_err_t ret;
    if (config_.node_type == ReservedTypes::HUB) {
        ret = scanner_->init(config_.node_id, config_.node_type, tx_manager_.get(), nullptr);
    }
    else {
        ret = scanner_->init(config_.node_id, config_.node_type, nullptr, this);
    }
    return ret;
}

esp_err_t EspNowManager::init_heartbeat_manager()
{
    if (heartbeat_manager_ == nullptr) {
        return ESP_FAIL;
    }
    esp_err_t ret = heartbeat_manager_->init(config_.heartbeat_interval_ms, config_.node_type);
    if (ret == ESP_OK) {
        heartbeat_manager_->update_node_id(config_.node_id);
    }
    return ret;
}

esp_err_t EspNowManager::init_pairing_manager()
{
    if (pairing_manager_ == nullptr) {
        return ESP_FAIL;
    }
    return pairing_manager_->init(config_.node_id, config_.node_type);
}

NodeState EspNowManager::determine_initial_state(etl::ivector<PeerInfo> &peers)
{
    if (peers.empty()) {
        return NodeState::PAIRING;
    }
    else {
        return NodeState::OPERATIONAL;
    }
}

void EspNowManager::add_peers_to_espnow(etl::ivector<PeerInfo> &peers)
{
    for (auto &peer : peers) {
        esp_now_peer_info_t info = {};
        memcpy(info.peer_addr, peer.mac, 6);
        info.channel = peer.channel;
        info.ifidx = WIFI_IF_STA;
        info.encrypt = false;
        hal_wifi_->hal_esp_now_add_peer(&info);
    }
}

esp_err_t EspNowManager::init_fail(esp_err_t ret, const char *step)
{
    ESP_LOGE(TAG, "init failed at %s: %s", step, esp_err_to_name(ret));
    deinit();
    return ret;
}

void EspNowManager::signal_tasks_to_stop()
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

void EspNowManager::delete_tasks()
{
    if (rx_task_handle_ != nullptr) {
        hal_freertos_->task_suspend(rx_task_handle_);
        hal_freertos_->task_delete(rx_task_handle_);
        rx_task_handle_ = nullptr;
    }
}

void EspNowManager::cleanup_resources()
{
    if (rx_dispatch_queue_ != nullptr) {
        hal_freertos_->queue_delete(rx_dispatch_queue_);
        rx_dispatch_queue_ = nullptr;
    }
    if (ack_mutex_ != nullptr) {
        hal_freertos_->semaphore_delete(ack_mutex_);
        ack_mutex_ = nullptr;
    }
}
