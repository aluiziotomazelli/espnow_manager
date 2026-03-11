#include <algorithm>
#include <cstring>
#include <inttypes.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "bootstrapper.hpp"
#include "channel_scanner.hpp"
#include "espnow_manager.hpp"
#include "heartbeat_manager.hpp"
#include "message_codec.hpp"
#include "message_router.hpp"
#include "pairing_manager.hpp"
#include "peer_manager.hpp"
#include "protocol_messages.hpp"
#include "timer_hal.hpp"
#include "tx_manager.hpp"
#include "tx_state_machine.hpp"
#include "wifi_hal.hpp"
#include "bootstrapper.hpp"

static const char *TAG = "EspNow";

// --- Singleton ---
EspNowManager &EspNowManager::instance()
{
    static StorageManager storage;
    static auto driver_hal = std::make_unique<WiFiHAL>();
    static auto timer_hal = std::make_unique<TimerHAL>();
    static auto bootstraper = std::make_unique<Bootstrapper>(*driver_hal);
    static auto peer_manager = std::make_unique<PeerManager>(storage, *driver_hal);
    static auto message_codec = std::make_unique<MessageCodec>();
    static auto scanner =
        std::make_unique<ChannelScanner>(*driver_hal, *message_codec, ReservedIds::HUB, ReservedTypes::HUB);
    static auto tx_fsm = std::make_unique<TxStateMachine>();
    static auto tx_manager = std::make_unique<TxManager>(*tx_fsm, *scanner, *driver_hal, *message_codec);
    static auto heartbeat_mgr =
        std::make_unique<HeartbeatManager>(*tx_manager, *peer_manager, *message_codec, ReservedIds::HUB);
    static auto pairing_mgr = std::make_unique<PairingManager>(*tx_manager, *peer_manager, *message_codec);
    static auto message_router =
        std::make_unique<MessageRouter>(*peer_manager, *tx_manager, *heartbeat_mgr, *pairing_mgr, *message_codec);

    static EspNowManager instance(
        std::move(driver_hal),
        std::move(timer_hal),
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
    std::unique_ptr<IBootstrapper> bootstraper,
    std::unique_ptr<IPeerManager> peer_manager,
    std::unique_ptr<IMessageCodec> message_codec,
    std::unique_ptr<IChannelScanner> scanner,
    std::unique_ptr<ITxStateMachine> tx_fsm,
    std::unique_ptr<ITxManager> tx_manager,
    std::unique_ptr<IHeartbeatManager> heartbeat_manager,
    std::unique_ptr<IPairingManager> pairing_manager,
    std::unique_ptr<IMessageRouter> message_router)
    : driver_hal_(std::move(driver_hal))
    , timer_hal_(std::move(timer_hal))
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
    if (!is_initialized_ && !esp_now_initialized_ && rx_dispatch_queue_ == nullptr &&
        transport_worker_queue_ == nullptr && ack_mutex_ == nullptr && rx_dispatch_task_handle_ == nullptr &&
        transport_worker_task_handle_ == nullptr) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing EspNow component...");

    if (tx_manager_)
        tx_manager_->deinit();
    if (heartbeat_manager_)
        heartbeat_manager_->deinit();
    if (pairing_manager_)
        pairing_manager_->deinit();

    // Signal tasks to stop
    if (rx_dispatch_task_handle_ != nullptr) {
        xTaskNotify(rx_dispatch_task_handle_, NOTIFY_STOP, eSetBits);
    }
    if (transport_worker_task_handle_ != nullptr) {
        xTaskNotify(transport_worker_task_handle_, NOTIFY_STOP, eSetBits);
    }

    // Send packets to weakup tasks
    RxPacket stop_packet = {};
    if (rx_dispatch_queue_ != nullptr)
        xQueueSend(rx_dispatch_queue_, &stop_packet, 0);
    if (transport_worker_queue_ != nullptr)
        xQueueSend(transport_worker_queue_, &stop_packet, 0);

    // Wait for tasks to exit (up to 1s).
    int timeout = 1000;
    while ((rx_dispatch_task_handle_ != nullptr || transport_worker_task_handle_ != nullptr) && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (timeout <= 0 && (rx_dispatch_task_handle_ != nullptr || transport_worker_task_handle_ != nullptr)) {
        ESP_LOGW(TAG, "Tasks did not terminate gracefully within timeout");
    }

    // Delete peers
    if (esp_now_initialized_ && peer_manager_) {
        std::vector<PeerInfo> peers = peer_manager_->get_all();
        for (const auto &peer : peers) {
            driver_hal_->hal_esp_now_del_peer(peer.mac);
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
    is_initialized_ = false;

    ESP_LOGI(TAG, "EspNow component deinitialized.");
    return ESP_OK;
}

esp_err_t EspNowManager::init(const EspNowConfig &config)
{
    if (is_initialized_)
        return ESP_ERR_INVALID_STATE;
    if (config.app_rx_queue == nullptr)
        return ESP_ERR_INVALID_ARG;

    config_ = config;
    esp_err_t ret = ESP_OK;

    uint8_t stored_channel;
    if (peer_manager_->load_from_storage(stored_channel) == ESP_OK) {
        config_.wifi_channel = stored_channel;
    }

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

    ret = tx_manager_->init(config_.stack_size_tx_manager, 9);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "tx_manager init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    heartbeat_manager_->update_node_id(config_.node_id);
    if (scanner_)
        scanner_->update_node_info(config_.node_id, config_.node_type);
    if (message_router_) {
        message_router_->set_app_queue(config_.app_rx_queue);
        message_router_->set_node_info(config_.node_id, config_.node_type);
    }

    {
        std::vector<PeerInfo> peers = peer_manager_->get_all();
        for (auto &peer : peers) {
            esp_now_peer_info_t info = {};
            memcpy(info.peer_addr, peer.mac, 6);
            info.channel = peer.channel;
            info.ifidx = WIFI_IF_STA;
            info.encrypt = false;
            driver_hal_->hal_esp_now_add_peer(&info);
        }
    }

    ret = heartbeat_manager_->init(config_.heartbeat_interval_ms, config_.node_type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "heartbeat_manager init failed: %s", esp_err_to_name(ret));
        goto fail;
    }
    ret = pairing_manager_->init(config_.node_type, config_.node_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "pairing_manager init failed: %s", esp_err_to_name(ret));
        goto fail;
    }

    is_initialized_ = true;
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

    auto encoded = message_codec_->encode(header, payload, len);
    if (encoded.empty())
        return ESP_ERR_INVALID_ARG;

    tx_packet.len = encoded.size();
    memcpy(tx_packet.data, encoded.data(), tx_packet.len);
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

    auto encoded = message_codec_->encode(header, payload, len);
    if (encoded.empty())
        return ESP_ERR_INVALID_ARG;

    tx_packet.len = encoded.size();
    memcpy(tx_packet.data, encoded.data(), tx_packet.len);
    tx_packet.requires_ack = require_ack;

    return tx_manager_->queue_packet(tx_packet);
}

esp_err_t EspNowManager::confirm_reception(AckStatus status)
{
    if (xSemaphoreTake(ack_mutex_, pdMS_TO_TICKS(100)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    if (!last_header_requiring_ack_.has_value()) {
        xSemaphoreGive(ack_mutex_);
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
        xSemaphoreGive(ack_mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    auto encoded = message_codec_->encode(ack.header, &ack.ack_sequence, sizeof(AckMessage) - sizeof(MessageHeader));
    if (encoded.empty()) {
        last_header_requiring_ack_.reset();
        xSemaphoreGive(ack_mutex_);
        return ESP_FAIL;
    }

    tx_packet.len = encoded.size();
    memcpy(tx_packet.data, encoded.data(), tx_packet.len);
    tx_packet.requires_ack = false;

    esp_err_t err = tx_manager_->queue_packet(tx_packet);
    last_header_requiring_ack_.reset();
    xSemaphoreGive(ack_mutex_);
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
esp_err_t EspNowManager::add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type)
{
    return peer_manager_->add(node_id, mac, channel, type);
}
esp_err_t EspNowManager::remove_peer(NodeId node_id)
{
    return peer_manager_->remove(node_id);
}
esp_err_t EspNowManager::start_pairing(uint32_t timeout_ms)
{
    return pairing_manager_->start(timeout_ms);
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
    packet.timestamp_us = esp_timer_get_time();
    xQueueSendFromISR(instance().rx_dispatch_queue_, &packet, 0);
}

void EspNowManager::esp_now_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    if (info->tx_status == WIFI_SEND_FAIL)
        instance().tx_manager_->notify_physical_fail();
}

void EspNowManager::rx_dispatch_task(void *arg)
{
    EspNowManager *self = static_cast<EspNowManager *>(arg);
    RxPacket packet;
    while (true) {
        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE && (notifications & NOTIFY_STOP))
            break;
        if (xQueueReceive(self->rx_dispatch_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Check for stop packet (empty packet or specific flag)
            if (packet.len == 0) {
                uint32_t notif = 0;
                if (xTaskNotifyWait(0, NOTIFY_STOP, &notif, 0) == pdTRUE && (notif & NOTIFY_STOP))
                    break;
            }

            if (!self->message_codec_->validate_crc(packet.data, packet.len))
                continue;
            auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
            if (!header_opt)
                continue;
            const MessageHeader *header = &header_opt.value();

            if (self->message_router_->should_dispatch_to_worker(header->msg_type)) {
                xQueueSend(self->transport_worker_queue_, &packet, 0);
            }
            else {
                if (header->requires_ack) {
                    if (xSemaphoreTake(self->ack_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
                        self->last_header_requiring_ack_ = *header;
                        xSemaphoreGive(self->ack_mutex_);
                    }
                }
                self->message_router_->handle_packet(packet);
            }
        }
    }
    self->rx_dispatch_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

void EspNowManager::transport_worker_task(void *arg)
{
    EspNowManager *self = static_cast<EspNowManager *>(arg);
    RxPacket packet;
    while (true) {
        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE && (notifications & NOTIFY_STOP))
            break;
        if (xQueueReceive(self->transport_worker_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (packet.len == 0) {
                uint32_t notif = 0;
                if (xTaskNotifyWait(0, NOTIFY_STOP, &notif, 0) == pdTRUE && (notif & NOTIFY_STOP))
                    break;
            }

            auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
            if (!header_opt)
                continue;
            const MessageHeader &header = header_opt.value();

            self->message_router_->handle_packet(packet);

            // Special handling for channel updates that affect the global config
            if (header.msg_type == MessageType::HEARTBEAT_RESPONSE) {
                auto resp = reinterpret_cast<const HeartbeatResponse *>(packet.data);
                self->update_wifi_channel(resp->wifi_channel);
            }
            else if (header.msg_type == MessageType::CHANNEL_SCAN_RESPONSE) {
                uint8_t ch;
                esp_wifi_get_channel(&ch, nullptr);
                self->update_wifi_channel(ch);
            }
        }
    }
    self->transport_worker_task_handle_ = nullptr;
    vTaskDelete(NULL);
}

uint64_t EspNowManager::get_time_ms() const
{
    return esp_timer_get_time() / 1000;
}

void EspNowManager::update_wifi_channel(uint8_t channel)
{
    if (config_.wifi_channel != channel) {
        config_.wifi_channel = channel;
        esp_now_peer_info_t broadcast = {};
        const uint8_t b_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(broadcast.peer_addr, b_mac, 6);
        broadcast.channel = channel;
        broadcast.ifidx = WIFI_IF_STA;
        broadcast.encrypt = false;
        esp_now_mod_peer(&broadcast);
        peer_manager_->persist(channel);
    }
}
