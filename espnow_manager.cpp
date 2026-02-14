#include "espnow_manager.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "channel_scanner.hpp"
#include "heartbeat_manager.hpp"
#include "message_codec.hpp"
#include "pairing_manager.hpp"
#include "peer_manager.hpp"
#include "protocol_messages.hpp"
#include "tx_manager.hpp"
#include "tx_state_machine.hpp"
#include "wifi_hal.hpp"
#include "message_router.hpp"
#include <algorithm>
#include <cstring>
#include <inttypes.h>

static const char *TAG = "EspNow";

// --- Singleton ---
EspNow &EspNow::instance()
{
    static EspNowStorage storage;
    static auto peer_manager = std::make_unique<RealPeerManager>(storage);
    static auto message_codec = std::make_unique<RealMessageCodec>();

    static RealWiFiHAL wifi_hal;
    static RealTxStateMachine tx_fsm;
    static RealChannelScanner scanner(wifi_hal, *message_codec, ReservedIds::HUB, ReservedTypes::HUB);

    static auto tx_manager = std::make_unique<RealTxManager>(tx_fsm, scanner, wifi_hal, *message_codec);

    static auto heartbeat_mgr = std::make_unique<RealHeartbeatManager>(*tx_manager, *peer_manager, *message_codec, ReservedIds::HUB);
    static auto pairing_mgr = std::make_unique<RealPairingManager>(*tx_manager, *peer_manager, *message_codec);
    static auto message_router = std::make_unique<RealMessageRouter>(*peer_manager, *tx_manager, *heartbeat_mgr, *pairing_mgr, *message_codec);

    static EspNow instance(std::move(peer_manager), std::move(tx_manager), &scanner, std::move(message_codec), std::move(heartbeat_mgr), std::move(pairing_mgr), std::move(message_router));
    return instance;
}

EspNow::EspNow(std::unique_ptr<IPeerManager> peer_manager,
               std::unique_ptr<ITxManager> tx_manager,
               IChannelScanner *scanner_ptr,
               std::unique_ptr<IMessageCodec> message_codec,
               std::unique_ptr<IHeartbeatManager> heartbeat_manager,
               std::unique_ptr<IPairingManager> pairing_manager,
               std::unique_ptr<IMessageRouter> message_router)
    : peer_manager_(std::move(peer_manager))
    , tx_manager_(std::move(tx_manager))
    , scanner_ptr_(scanner_ptr)
    , message_codec_(std::move(message_codec))
    , heartbeat_manager_(std::move(heartbeat_manager))
    , pairing_manager_(std::move(pairing_manager))
    , message_router_(std::move(message_router))
{
}

EspNow::~EspNow()
{
    deinit();
}

esp_err_t EspNow::deinit()
{
    if (!is_initialized_) {
        return ESP_OK;
    }

    is_initialized_ = false;
    ESP_LOGI(TAG, "Deinitializing EspNow component...");

    if (tx_manager_) tx_manager_->deinit();
    if (heartbeat_manager_) heartbeat_manager_->deinit();
    if (pairing_manager_) pairing_manager_->deinit();

    if (rx_dispatch_task_handle_ != nullptr) {
        xTaskNotify(rx_dispatch_task_handle_, NOTIFY_STOP, eSetBits);
    }
    if (transport_worker_task_handle_ != nullptr) {
        xTaskNotify(transport_worker_task_handle_, NOTIFY_STOP, eSetBits);
    }

    RxPacket stop_packet = {};
    if (rx_dispatch_queue_ != nullptr) xQueueSend(rx_dispatch_queue_, &stop_packet, 0);
    if (transport_worker_queue_ != nullptr) xQueueSend(transport_worker_queue_, &stop_packet, 0);

    // Wait for tasks to exit.
    int timeout = 20; // 200ms
    while ((rx_dispatch_task_handle_ != nullptr || transport_worker_task_handle_ != nullptr) && timeout-- > 0) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Tasks should have deleted themselves.

    std::vector<PeerInfo> peers = peer_manager_->get_all();
    for (const auto &peer : peers) esp_now_del_peer(peer.mac);
    esp_now_deinit();

    if (rx_dispatch_queue_ != nullptr) vQueueDelete(rx_dispatch_queue_);
    if (transport_worker_queue_ != nullptr) vQueueDelete(transport_worker_queue_);

    if (ack_mutex_ != nullptr) {
        vSemaphoreDelete(ack_mutex_);
        ack_mutex_ = nullptr;
    }

    last_header_requiring_ack_.reset();
    config_ = EspNowConfig();

    ESP_LOGI(TAG, "EspNow component deinitialized.");
    return ESP_OK;
}

esp_err_t EspNow::init(const EspNowConfig &config)
{
    if (is_initialized_) return ESP_ERR_INVALID_STATE;
    if (config.app_rx_queue == nullptr) return ESP_ERR_INVALID_ARG;

    config_ = config;

    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) return ESP_ERR_INVALID_STATE;

    uint8_t stored_channel;
    if (peer_manager_->load_from_storage(stored_channel) == ESP_OK) {
        config_.wifi_channel = stored_channel;
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));
    ESP_ERROR_CHECK(esp_wifi_set_channel(config_.wifi_channel, WIFI_SECOND_CHAN_NONE));

    esp_now_peer_info_t broadcast_peer = {};
    const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = config_.wifi_channel;
    broadcast_peer.ifidx = WIFI_IF_STA;
    broadcast_peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&broadcast_peer));

    ack_mutex_ = xSemaphoreCreateMutex();
    if (ack_mutex_ == nullptr) return ESP_FAIL;

    rx_dispatch_queue_ = xQueueCreate(30, sizeof(RxPacket));
    transport_worker_queue_ = xQueueCreate(20, sizeof(RxPacket));
    if (rx_dispatch_queue_ == nullptr || transport_worker_queue_ == nullptr) return ESP_FAIL;

    if (xTaskCreate(rx_dispatch_task, "espnow_dispatch", config_.stack_size_rx_dispatch, this, 10, &rx_dispatch_task_handle_) != pdPASS) return ESP_FAIL;
    if (xTaskCreate(transport_worker_task, "espnow_worker", config_.stack_size_transport_worker, this, 5, &transport_worker_task_handle_) != pdPASS) return ESP_FAIL;

    if (tx_manager_->init(config_.stack_size_tx_manager, 9) != ESP_OK) return ESP_FAIL;

    is_initialized_ = true;

    heartbeat_manager_->update_node_id(config_.node_id);
    if (scanner_ptr_) scanner_ptr_->update_node_info(config_.node_id, config_.node_type);
    if (message_router_) {
        message_router_->set_app_queue(config_.app_rx_queue);
        message_router_->set_node_info(config_.node_id, config_.node_type);
    }

    std::vector<PeerInfo> peers = peer_manager_->get_all();
    for (auto &peer : peers) {
        esp_now_peer_info_t info = {};
        memcpy(info.peer_addr, peer.mac, 6);
        info.channel = peer.channel;
        info.ifidx = WIFI_IF_STA;
        info.encrypt = false;
        esp_now_add_peer(&info);
    }

    if (heartbeat_manager_->init(config_.heartbeat_interval_ms, config_.node_type) != ESP_OK) return ESP_FAIL;
    if (pairing_manager_->init(config_.node_type, config_.node_id) != ESP_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "EspNow component initialized successfully.");
    return ESP_OK;
}

esp_err_t EspNow::send_data(NodeId dest_node_id, PayloadType payload_type, const void *payload, size_t len, bool require_ack)
{
    TxPacket tx_packet;
    if (!peer_manager_->find_mac(dest_node_id, tx_packet.dest_mac)) return ESP_ERR_NOT_FOUND;

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
    if (encoded.empty()) return ESP_ERR_INVALID_ARG;

    tx_packet.len = encoded.size();
    memcpy(tx_packet.data, encoded.data(), tx_packet.len);
    tx_packet.requires_ack = require_ack;

    return tx_manager_->queue_packet(tx_packet);
}

esp_err_t EspNow::send_command(NodeId dest_node_id, CommandType command_type, const void *payload, size_t len, bool require_ack)
{
    TxPacket tx_packet;
    if (!peer_manager_->find_mac(dest_node_id, tx_packet.dest_mac)) return ESP_ERR_NOT_FOUND;

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
    if (encoded.empty()) return ESP_ERR_INVALID_ARG;

    tx_packet.len = encoded.size();
    memcpy(tx_packet.data, encoded.data(), tx_packet.len);
    tx_packet.requires_ack = require_ack;

    return tx_manager_->queue_packet(tx_packet);
}

esp_err_t EspNow::confirm_reception(AckStatus status)
{
    if (xSemaphoreTake(ack_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return ESP_ERR_TIMEOUT;
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

std::vector<PeerInfo> EspNow::get_peers() { return peer_manager_->get_all(); }
std::vector<NodeId> EspNow::get_offline_peers() const { return peer_manager_->get_offline(get_time_ms()); }
esp_err_t EspNow::add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type) { return peer_manager_->add(node_id, mac, channel, type); }
esp_err_t EspNow::remove_peer(NodeId node_id) { return peer_manager_->remove(node_id); }
esp_err_t EspNow::start_pairing(uint32_t timeout_ms) { return pairing_manager_->start(timeout_ms); }

void EspNow::esp_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!info || !data || len <= 0 || len > ESP_NOW_MAX_DATA_LEN) return;
    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len = len;
    packet.rssi = info->rx_ctrl->rssi;
    packet.timestamp_us = esp_timer_get_time();
    xQueueSendFromISR(instance().rx_dispatch_queue_, &packet, 0);
}

void EspNow::esp_now_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    if (info->tx_status == WIFI_SEND_FAIL) instance().tx_manager_->notify_physical_fail();
}

void EspNow::rx_dispatch_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;
    while (true) {
        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE && (notifications & NOTIFY_STOP)) break;
        if (xQueueReceive(self->rx_dispatch_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Check for stop packet (empty packet or specific flag)
            if (packet.len == 0) {
                 uint32_t notif = 0;
                 if (xTaskNotifyWait(0, NOTIFY_STOP, &notif, 0) == pdTRUE && (notif & NOTIFY_STOP)) break;
            }

            if (!self->message_codec_->validate_crc(packet.data, packet.len)) continue;
            auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
            if (!header_opt) continue;
            const MessageHeader *header = &header_opt.value();

            if (self->message_router_->should_dispatch_to_worker(header->msg_type)) {
                xQueueSend(self->transport_worker_queue_, &packet, 0);
            } else {
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

void EspNow::transport_worker_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;
    while (true) {
        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE && (notifications & NOTIFY_STOP)) break;
        if (xQueueReceive(self->transport_worker_queue_, &packet, pdMS_TO_TICKS(100)) == pdTRUE) {
             if (packet.len == 0) {
                 uint32_t notif = 0;
                 if (xTaskNotifyWait(0, NOTIFY_STOP, &notif, 0) == pdTRUE && (notif & NOTIFY_STOP)) break;
            }

            auto header_opt = self->message_codec_->decode_header(packet.data, packet.len);
            if (!header_opt) continue;
            const MessageHeader &header = header_opt.value();

            self->message_router_->handle_packet(packet);

            // Special handling for channel updates that affect the global config
            if (header.msg_type == MessageType::HEARTBEAT_RESPONSE) {
                auto resp = reinterpret_cast<const HeartbeatResponse *>(packet.data);
                self->update_wifi_channel(resp->wifi_channel);
            } else if (header.msg_type == MessageType::CHANNEL_SCAN_RESPONSE) {
                uint8_t ch;
                esp_wifi_get_channel(&ch, nullptr);
                self->update_wifi_channel(ch);
            }
        }
    }
    self->transport_worker_task_handle_ = nullptr;
    vTaskDelete(NULL);
}


uint64_t EspNow::get_time_ms() const { return esp_timer_get_time() / 1000; }

void EspNow::update_wifi_channel(uint8_t channel)
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
