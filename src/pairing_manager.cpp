#include "pairing_manager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "PairingMgr";

RealPairingManager::RealPairingManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, IMessageCodec &codec)
    : tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , codec_(codec)
{
    mutex_ = xSemaphoreCreateMutex();
}

RealPairingManager::~RealPairingManager()
{
    deinit();
    if (mutex_)
        vSemaphoreDelete(mutex_);
}

esp_err_t RealPairingManager::init(NodeType type, NodeId id)
{
    my_type_ = type;
    my_id_   = id;
    return ESP_OK;
}

esp_err_t RealPairingManager::deinit()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (timeout_timer_) {
        xTimerDelete(timeout_timer_, portMAX_DELAY);
        timeout_timer_ = nullptr;
    }
    if (periodic_timer_) {
        xTimerDelete(periodic_timer_, portMAX_DELAY);
        periodic_timer_ = nullptr;
    }
    is_active_ = false;
    xSemaphoreGive(mutex_);
    return ESP_OK;
}

esp_err_t RealPairingManager::start(uint32_t timeout_ms)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (is_active_) {
        xSemaphoreGive(mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Pairing started for %u ms.", (unsigned int)timeout_ms);

    timeout_timer_ = xTimerCreate("pair_timeout", pdMS_TO_TICKS(timeout_ms), pdFALSE, this, timeout_cb);
    if (my_type_ != ReservedTypes::HUB) {
        periodic_timer_ = xTimerCreate("pair_periodic", pdMS_TO_TICKS(5000), pdTRUE, this, periodic_cb);
        xTimerStart(periodic_timer_, 0);
        send_pair_request();
    }
    xTimerStart(timeout_timer_, 0);
    is_active_ = true;
    xSemaphoreGive(mutex_);
    return ESP_OK;
}

void RealPairingManager::handle_request(const RxPacket &packet)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!is_active_ || my_type_ != ReservedTypes::HUB) {
        xSemaphoreGive(mutex_);
        return;
    }
    xSemaphoreGive(mutex_);

    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt)
        return;
    const MessageHeader &header = header_opt.value();
    const PairRequest *req      = reinterpret_cast<const PairRequest *>(packet.data);

    ESP_LOGI(TAG, "Pair request from Node ID %d", (int)header.sender_node_id);

    PairResponse resp;
    resp.header.msg_type        = MessageType::PAIR_RESPONSE;
    resp.header.sender_node_id  = my_id_;
    resp.header.sender_type     = my_type_;
    resp.header.dest_node_id    = header.sender_node_id;
    resp.header.sequence_number = 0;

    if (header.sender_type == ReservedTypes::HUB) {
        resp.status = PairStatus::REJECTED_NOT_ALLOWED;
    }
    else {
        peer_mgr_.add(header.sender_node_id, packet.src_mac, 1, header.sender_type, req->heartbeat_interval_ms);
        resp.status       = PairStatus::ACCEPTED;
        resp.wifi_channel = 1; // Needs real channel
    }

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    auto encoded = codec_.encode(resp.header, &resp.status, sizeof(PairResponse) - sizeof(MessageHeader));
    if (!encoded.empty()) {
        tx_packet.len = encoded.size();
        memcpy(tx_packet.data, encoded.data(), tx_packet.len);
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void RealPairingManager::handle_response(const RxPacket &packet)
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (!is_active_ || my_type_ == ReservedTypes::HUB) {
        xSemaphoreGive(mutex_);
        return;
    }

    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt) {
        xSemaphoreGive(mutex_);
        return;
    }

    const PairResponse *resp = reinterpret_cast<const PairResponse *>(packet.data);
    if (resp->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub.");
        peer_mgr_.add(header_opt->sender_node_id, packet.src_mac, resp->wifi_channel, header_opt->sender_type);
        is_active_ = false;
        if (periodic_timer_) {
            xTimerStop(periodic_timer_, 0);
        }
        if (timeout_timer_) {
            xTimerStop(timeout_timer_, 0);
        }
    }
    xSemaphoreGive(mutex_);
}

void RealPairingManager::send_pair_request()
{
    PairRequest req;
    req.header.msg_type        = MessageType::PAIR_REQUEST;
    req.header.sender_node_id  = my_id_;
    req.header.sender_type     = my_type_;
    req.header.dest_node_id    = ReservedIds::HUB;
    req.header.sequence_number = 0;
    req.heartbeat_interval_ms  = 60000;

    TxPacket tx_packet;
    const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(tx_packet.dest_mac, broadcast_mac, 6);

    auto encoded = codec_.encode(req.header, &req.firmware_version, sizeof(PairRequest) - sizeof(MessageHeader));
    if (!encoded.empty()) {
        tx_packet.len = encoded.size();
        memcpy(tx_packet.data, encoded.data(), tx_packet.len);
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void RealPairingManager::timeout_cb(TimerHandle_t xTimer)
{
    static_cast<RealPairingManager *>(pvTimerGetTimerID(xTimer))->on_timeout();
}

void RealPairingManager::periodic_cb(TimerHandle_t xTimer)
{
    static_cast<RealPairingManager *>(pvTimerGetTimerID(xTimer))->send_pair_request();
}

void RealPairingManager::on_timeout()
{
    xSemaphoreTake(mutex_, portMAX_DELAY);
    is_active_ = false;
    if (periodic_timer_)
        xTimerStop(periodic_timer_, 0);
    ESP_LOGI(TAG, "Pairing timed out.");
    xSemaphoreGive(mutex_);
}
