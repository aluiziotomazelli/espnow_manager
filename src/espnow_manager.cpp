// src/espnow_manager.cpp
// #include <algorithm>
#include <cstring>
// #include <inttypes.h>

#include "esp_log.h"
// #include "esp_mac.h"
// #include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_attr.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
// #include "freertos/task.h"

#include "bootstrapper.hpp"
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
#include "bootstrapper.hpp"
#include "hal_freertos.hpp"
#include "hal_nvs.hpp"
#include "persistence_backend.hpp"
#include "storage_manager.hpp"

#include "espnow_manager.hpp"

static const char *TAG = "EspNow";

// RTC storage for peer list persistence must stay in global scope
static RTC_DATA_ATTR PersistentData g_rtc_storage;

// --- Singleton ---
EspNowManager &EspNowManager::instance()
{
    static NvsHAL nvs_hal;
    static auto rtc_backend = std::make_unique<RtcBackend>(g_rtc_storage);
    static auto nvs_backend = std::make_unique<NvsBackend>(nvs_hal);
    static StorageManager storage(std::move(rtc_backend), std::move(nvs_backend));

    static auto hal_wifi = std::make_unique<WiFiHAL>();
    static auto hal_timer = std::make_unique<TimerHAL>();
    static auto hal_freertos = std::make_unique<FreeRTOSHAL>();
    static auto bootstraper = std::make_unique<Bootstrapper>(*hal_wifi, *hal_freertos);
    static auto peer_manager = std::make_unique<PeerManager>(storage, *hal_wifi, *hal_freertos);
    static auto message_codec = std::make_unique<MessageCodec>();
    static auto scanner = std::make_unique<DiscoveryManager>(*hal_wifi, *message_codec, *hal_freertos);
    static auto tx_fsm = std::make_unique<TxStateMachine>();
    static auto tx_manager =
        std::make_unique<TxManager>(*tx_fsm, *scanner, *hal_wifi, *hal_freertos, *message_codec, 500);
    static auto heartbeat_mgr = std::make_unique<HeartbeatManager>(
        ReservedIds::HUB, *tx_manager, *peer_manager, *message_codec, *hal_freertos, *hal_timer);
    static auto pairing_mgr = std::make_unique<PairingManager>(*tx_manager, *peer_manager, *message_codec);
    static auto message_router =
        std::make_unique<MessageRouter>(*scanner, *tx_manager, *heartbeat_mgr, *pairing_mgr, *message_codec);

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

EspNowManager::EspNowManager(
    std::unique_ptr<IWiFiHAL> driver_hal,
    std::unique_ptr<ITimerHAL> timer_hal,
    std::unique_ptr<IFreeRTOSHAL> freertos_hal,
    std::unique_ptr<IBootstrapper> bootstraper,
    std::unique_ptr<IPeerManager> peer_manager,
    std::unique_ptr<IMessageCodec> message_codec,
    std::unique_ptr<IDiscoveryManager> scanner,
    std::unique_ptr<ITxStateMachine> tx_fsm,
    std::unique_ptr<ITxManager> tx_manager,
    std::unique_ptr<IHeartbeatManager> heartbeat_manager,
    std::unique_ptr<IPairingManager> pairing_manager,
    std::unique_ptr<IMessageRouter> message_router)
    : hal_driver_(std::move(driver_hal))
    , hal_timer_(std::move(timer_hal))
    , hal_freertos_(std::move(freertos_hal))
    , bootstrapper_(std::move(bootstraper))
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
    deinit();
}

esp_err_t EspNowManager::deinit()
{
    if (node_state_.load() == NodeState::UNINITIALIZED && !esp_now_initialized_ && rx_dispatch_queue_ == nullptr &&
        transport_worker_queue_ == nullptr && ack_mutex_ == nullptr && rx_dispatch_task_handle_ == nullptr &&
        transport_worker_task_handle_ == nullptr) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing EspNow component...");

    if (tx_manager_)
        tx_manager_->deinit();
    if (heartbeat_manager_)
        heartbeat_manager_->deinit();

    // Signal tasks to stop
    if (rx_dispatch_task_handle_ != nullptr) {
        hal_freertos_->task_notify(rx_dispatch_task_handle_, NOTIFY_STOP, eSetBits);
    }
    if (transport_worker_task_handle_ != nullptr) {
        hal_freertos_->task_notify(transport_worker_task_handle_, NOTIFY_STOP, eSetBits);
    }

    // Send packets to weakup tasks
    RxPacket stop_packet = {};
    if (rx_dispatch_queue_ != nullptr)
        hal_freertos_->queue_send(rx_dispatch_queue_, &stop_packet, 0);
    if (transport_worker_queue_ != nullptr)
        hal_freertos_->queue_send(transport_worker_queue_, &stop_packet, 0);

    // Wait for tasks to exit (up to 1s).
    int timeout = 1000;
    while ((rx_dispatch_task_handle_ != nullptr || transport_worker_task_handle_ != nullptr) && timeout-- > 0) {
        hal_freertos_->task_delay(pdMS_TO_TICKS(10));
    }

    if (timeout <= 0 && (rx_dispatch_task_handle_ != nullptr || transport_worker_task_handle_ != nullptr)) {
        ESP_LOGW(TAG, "Tasks did not terminate gracefully within timeout");
    }

    // Delete peers
    if (esp_now_initialized_ && peer_manager_) {
        std::vector<PeerInfo> peers = peer_manager_->get_all();
        for (const auto &peer : peers) {
            hal_driver_->hal_esp_now_del_peer(peer.mac);
        }
    }

    // Call bootstraper to delete queues, mutex and task if not terminated gracefully
    if (bootstrapper_) {
        bootstrapper_->deinit(
            rx_dispatch_queue_,
            transport_worker_queue_,
            ack_mutex_,
            rx_dispatch_task_handle_,
            transport_worker_task_handle_);
    }

    // Reset state
    esp_now_initialized_ = false;
    last_header_requiring_ack_.reset();
    config_ = EspNowConfig();
    node_state_.store(NodeState::UNINITIALIZED);

    ESP_LOGI(TAG, "EspNow component deinitialized.");
    return ESP_OK;
}

esp_err_t EspNowManager::init(const EspNowConfig &config)
{
    if (node_state_.load() != NodeState::UNINITIALIZED)
        return ESP_ERR_INVALID_STATE;
    if (config.app_rx_queue == nullptr)
        return ESP_ERR_INVALID_ARG;

    config_ = config;
    esp_err_t ret = ESP_OK;

    // PeerManager needs to be initialized to load channel from storage before bootstraper
    if (peer_manager_) {
        uint8_t stored_channel;
        if (peer_manager_->load_from_storage(stored_channel) == ESP_OK) {
            config_.wifi_channel = stored_channel;
        }
    }

    // BootStrapper initializes ESPNOW, creates tasks, queues and mutexes
    if (bootstrapper_) {
        EspNowBootstrapConfig bootstrap_cfg = {};
        bootstrap_cfg.recv_cb = esp_now_recv_cb;
        bootstrap_cfg.send_cb = esp_now_send_cb;
        bootstrap_cfg.rx_dispatch_fn = EspNowManager::rx_dispatch_task;
        bootstrap_cfg.transport_worker_fn = EspNowManager::transport_worker_task;
        bootstrap_cfg.task_params = this;

        ret = bootstrapper_->init(
            config_,
            bootstrap_cfg,
            rx_dispatch_queue_,
            transport_worker_queue_,
            ack_mutex_,
            rx_dispatch_task_handle_,
            transport_worker_task_handle_);

        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize bootstraper: %s", esp_err_to_name(ret));
            goto fail;
        }
        esp_now_initialized_ = true;
    }

    // TxManager
    if (tx_manager_) {
        ret = tx_manager_->init(config_.stack_size_tx_manager, 9);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "tx_manager init failed: %s", esp_err_to_name(ret));
            goto fail;
        }
    }

    // DiscoveryManager
    if (scanner_) {
        if (config_.node_type == ReservedTypes::HUB) {
            ret = scanner_->init(config_.node_id, config_.node_type, tx_manager_.get(), nullptr);
        }
        else {
            ret = scanner_->init(config_.node_id, config_.node_type, nullptr, this);
        }
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize discovery manager: %s", esp_err_to_name(ret));
            goto fail;
        }
    }

    // HeartbeatManager
    if (heartbeat_manager_) {
        ret = heartbeat_manager_->init(config_.heartbeat_interval_ms, config_.node_type);
        if (ret == ESP_OK) {
            heartbeat_manager_->update_node_id(config_.node_id);
        }
        else {
            ESP_LOGE(TAG, "heartbeat_manager init failed: %s", esp_err_to_name(ret));
            goto fail;
        }
    }

    // PairingManager
    if (pairing_manager_) {
        ret = pairing_manager_->init(config_.node_type, config_.node_id);
        if (ret == ESP_OK) {
        }
        else {
            ESP_LOGE(TAG, "pairing_manager init failed: %s", esp_err_to_name(ret));
            goto fail;
        }
    }

    // MessageRouter
    if (message_router_) {
        message_router_->set_app_queue(config_.app_rx_queue);
        message_router_->set_node_info(config_.node_id, config_.node_type);
    }

    // Add peers to ESPNOW
    {
        std::vector<PeerInfo> peers = peer_manager_->get_all();
        if (peers.empty()) {
            node_state_ = NodeState::PAIRING;
        }
        else {
            node_state_ = NodeState::OPERATIONAL;

            for (auto &peer : peers) {
                esp_now_peer_info_t info = {};
                memcpy(info.peer_addr, peer.mac, 6);
                info.channel = peer.channel;
                info.ifidx = WIFI_IF_STA;
                info.encrypt = false;
                hal_driver_->hal_esp_now_add_peer(&info);
            }
        }
    }

    // Update sub componentes with current channel from loaded NVS or Config default
    propagate_channel();

    ESP_LOGI(TAG, "EspNow component initialized successfully.");
    return ESP_OK;

fail:
    deinit();
    return ret;
}

esp_err_t EspNowManager::send_data(
    NodeId dest_node_id,
    PayloadType payload_type,
    const void *payload,
    size_t len,
    bool require_ack)
{
    TxPacket tx_packet;
    if (!peer_manager_->find_mac(dest_node_id, tx_packet.dest_mac))
        return ESP_ERR_NOT_FOUND;

    MessageHeader header;
    header.msg_type = MessageType::DATA;
    header.sequence_number = 0;
    header.sender_type = config_.node_type;
    header.sender_node_id = config_.node_id;
    header.payload_type = payload_type;
    header.requires_ack = require_ack;
    header.dest_node_id = dest_node_id;
    header.timestamp_ms = get_time_ms();

    tx_packet.len = message_codec_->encode(header, payload, len, tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len == 0)
        return ESP_ERR_INVALID_ARG;

    tx_packet.requires_ack = require_ack;

    return tx_manager_->queue_packet(tx_packet);
}

esp_err_t EspNowManager::send_command(
    NodeId dest_node_id,
    CommandType command_type,
    const void *payload,
    size_t len,
    bool require_ack)
{
    TxPacket tx_packet;
    if (!peer_manager_->find_mac(dest_node_id, tx_packet.dest_mac))
        return ESP_ERR_NOT_FOUND;

    MessageHeader header;
    header.msg_type = MessageType::COMMAND;
    header.sequence_number = 0;
    header.sender_type = config_.node_type;
    header.sender_node_id = config_.node_id;
    header.payload_type = static_cast<PayloadType>(command_type);
    header.requires_ack = require_ack;
    header.dest_node_id = dest_node_id;
    header.timestamp_ms = get_time_ms();

    tx_packet.len = message_codec_->encode(header, payload, len, tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len == 0)
        return ESP_ERR_INVALID_ARG;

    tx_packet.requires_ack = require_ack;

    return tx_manager_->queue_packet(tx_packet);
}

esp_err_t EspNowManager::confirm_reception(AckStatus status)
{
    if (hal_freertos_->semaphore_take(ack_mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    if (!last_header_requiring_ack_.has_value()) {
        hal_freertos_->semaphore_give(ack_mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    const auto &header_to_ack = last_header_requiring_ack_.value();
    AckMessage ack;
    ack.header.msg_type = MessageType::ACK;
    ack.header.sender_node_id = config_.node_id;
    ack.header.sender_type = config_.node_type;
    ack.header.dest_node_id = header_to_ack.sender_node_id;
    ack.header.sequence_number = 0;
    ack.ack_sequence = header_to_ack.sequence_number;
    ack.status = status;

    TxPacket tx_packet;
    if (!peer_manager_->find_mac(header_to_ack.sender_node_id, tx_packet.dest_mac)) {
        last_header_requiring_ack_.reset();
        hal_freertos_->semaphore_give(ack_mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    tx_packet.len = message_codec_->encode(
        ack.header,
        &ack.ack_sequence,
        sizeof(AckMessage) - sizeof(MessageHeader),
        tx_packet.data,
        sizeof(tx_packet.data));
    if (tx_packet.len == 0) {
        last_header_requiring_ack_.reset();
        hal_freertos_->semaphore_give(ack_mutex_);
        return ESP_FAIL;
    }

    tx_packet.requires_ack = false;

    esp_err_t err = tx_manager_->queue_packet(tx_packet);
    last_header_requiring_ack_.reset();
    hal_freertos_->semaphore_give(ack_mutex_);
    return err;
}

std::vector<PeerInfo> EspNowManager::get_peers()
{
    return peer_manager_->get_all();
}
std::vector<NodeId> EspNowManager::get_offline_peers() const
{
    return peer_manager_->get_offline(get_time_ms());
}
esp_err_t EspNowManager::add_peer(NodeId node_id, const uint8_t *mac, NodeType type)
{
    return peer_manager_->add(node_id, mac, type);
}
esp_err_t EspNowManager::remove_peer(NodeId node_id)
{
    return peer_manager_->remove(node_id);
}
esp_err_t EspNowManager::start_pairing(uint32_t timeout_ms)
{
    if (node_state_ == NodeState::UNINITIALIZED) {
        return ESP_ERR_INVALID_STATE;
    }

    transition_to_state(NodeState::PAIRING);
    return pairing_manager_->start(timeout_ms, get_time_ms());
}

void EspNowManager::esp_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN)
        return;
    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len = len;
    packet.rssi = info->rx_ctrl->rssi;
    packet.timestamp_us = instance().hal_timer_->get_time_us();
    instance().hal_freertos_->queue_send_fromISR(instance().rx_dispatch_queue_, &packet, 0);
}

void EspNowManager::esp_now_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    if (info->tx_status == static_cast<wifi_tx_status_t>(ESP_NOW_SEND_FAIL))
        instance().tx_manager_->notify_physical_fail();
}

void EspNowManager::rx_dispatch_task(void *arg)
{
    EspNowManager *self = static_cast<EspNowManager *>(arg);
    RxPacket packet;
    while (true) {
        uint32_t notifications = 0;

        // Check for pending notifications without blocking (timeout = 0). We prioritize packet reception below
        // notifications are checked every ~100 ms as a side effect of the queue_receive timeout.
        // Clear all bits on exit (0xFFFFFFFF) so processed bits don't retrigger on the next iteration.
        // No bits are cleared on entry since we want to read whatever accumulated since last check.
        if (self->hal_freertos_->task_notify_wait(0, 0xFFFFFFFF, &notifications, 0) == pdTRUE) {
            // Entered scanning state — TxManager is actively searching for the HUB. NodeState transitions to
            // SCANNING so the rest of the system knows normal operation is suspended until the channel is rediscovered.
            if (notifications & NOTIFY_SCANNING) {
                self->transition_to_state(NodeState::SCANNING);
            }
            // NOTIFY_CHANNEL_FOUND is set by on_channel_found_cb(), which runs in the TxManager task
            // context. All work that touches EspNowManager state (config_, peer_manager_, node_state_)
            // is done here in the rx_dispatch_task to avoid cross-task data races.
            if (notifications & NOTIFY_CHANNEL_FOUND) {
                uint8_t channel = self->last_found_channel_.load();
                self->config_.wifi_channel = channel;
                self->propagate_channel();
                self->peer_manager_->persist();
                self->transition_to_state(NodeState::OPERATIONAL);
            }
            // NOTIFY_SCAN_FAILED is set by on_scan_failed_cb() when the
            // DiscoveryManager exhausts all channels without finding the HUB.
            // Transition to PAIRING so the node can attempt re-association.
            if (notifications & NOTIFY_SCAN_FAILED) {
                self->transition_to_state(NodeState::PAIRING);
            }
            // If NOTIFY_STOP is set, we break the loop and exit the task.
            if (notifications & NOTIFY_STOP) {
                break;
            }
        }

        // Wait for incoming packets with a timeout to periodically check for notifications.
        if (self->hal_freertos_->queue_receive(self->rx_dispatch_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // TODO: check for bootstrapper sending empty packet to stop the task,
            // since we are no longer blocking the task on queue receive, we dont need
            // more packets to weakup the task to check for notifications. The code bellow is for
            // documentation during the refactoring process

            // // Check for stop packet (empty packet or specific flag)
            // if (packet.len == 0) {
            //     uint32_t notif = 0;
            //     if (self->hal_freertos_->task_notify_wait(0, NOTIFY_STOP, &notif, 0) == pdTRUE && (notif &
            //     NOTIFY_STOP))
            //         break;
            // }

            if (!self->message_codec_->validate_crc(packet.data, packet.len))
                continue;
            auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
            if (!header_opt)
                continue;
            const MessageHeader &header = header_opt.value();

            if (self->message_router_->should_dispatch_to_worker(header.msg_type)) {
                self->hal_freertos_->queue_send(self->transport_worker_queue_, &packet, 0);
            }
            else {
                if (header.requires_ack) {
                    if (self->hal_freertos_->semaphore_take(self->ack_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
                        self->last_header_requiring_ack_ = header;
                        self->hal_freertos_->semaphore_give(self->ack_mutex_);
                    }
                }
                self->message_router_->handle_packet(packet);
            }
        }
    }
    self->rx_dispatch_task_handle_ = nullptr;
    self->hal_freertos_->task_suspend(NULL);
    self->hal_freertos_->task_delete(NULL);
}

void EspNowManager::transport_worker_task(void *arg)
{
    EspNowManager *self = static_cast<EspNowManager *>(arg);
    RxPacket packet;
    while (true) {
        uint32_t notifications = 0;
        if (self->hal_freertos_->task_notify_wait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE &&
            (notifications & NOTIFY_STOP))
            break;
        if (self->hal_freertos_->queue_receive(self->transport_worker_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // if (packet.len == 0) {
            //     uint32_t notif = 0;
            //     if (self->hal_freertos_->task_notify_wait(0, NOTIFY_STOP, &notif, 0) == pdTRUE && (notif &
            //     NOTIFY_STOP))
            //         break;
            // }
            // Delegate directly to router. Channel updates are now handled
            // via DiscoveryManager callbacks, avoiding redundant decoding here.
            self->message_router_->handle_packet(packet);
        }
        // Tick pairing manager to handle timeouts, has internaly safe guards to avoid
        // unnecessary processing when not in pairing mode
        self->pairing_manager_->tick(self->get_time_ms());
    }
    self->transport_worker_task_handle_ = nullptr;
    self->hal_freertos_->task_suspend(NULL);
    self->hal_freertos_->task_delete(NULL);
}

uint64_t EspNowManager::get_time_ms() const
{
    return esp_timer_get_time() / 1000;
}

// IChannelObserver implementation for DiscoveryManager callbacks
void EspNowManager::on_channel_found_cb(uint8_t channel)
{
    last_found_channel_.store(channel);
    hal_freertos_->task_notify(rx_dispatch_task_handle_, NOTIFY_CHANNEL_FOUND, eSetBits);

    // config_.wifi_channel = channel;
    // // Update broadcast peer channel
    // esp_now_peer_info_t broadcast = {};
    // memcpy(broadcast.peer_addr, BROADCAST_MAC, 6);
    // broadcast.channel = channel;
    // broadcast.ifidx = WIFI_IF_STA;
    // broadcast.encrypt = false;
    // hal_driver_->hal_esp_now_mod_peer(&broadcast);
    // propagate_channel();
    // peer_manager_->persist();
}

void EspNowManager::on_scan_failed_cb()
{
    hal_freertos_->task_notify(rx_dispatch_task_handle_, NOTIFY_SCAN_FAILED, eSetBits);
}

void EspNowManager::on_scan_started_cb()
{
    hal_freertos_->task_notify(rx_dispatch_task_handle_, NOTIFY_SCANNING, eSetBits);
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

bool EspNowManager::is_initialized() const
{
    return node_state_.load() != NodeState::UNINITIALIZED;
}