#include <cstring>

#include "esp_log.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
// #include "freertos/task.h"
// #include "freertos/timers.h"

#include "i_message_codec.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_hal_freertos.hpp"
#include "pairing_manager.hpp"
#include "protocol_types.hpp"

static const char *TAG = "PairingMgr";

PairingManager::PairingManager(
    ITxManager &tx_mgr,
    IPeerManager &peer_mgr,
    IMessageCodec &codec,
    IFreeRTOSHAL &hal_freertos)
    : tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , codec_(codec)
    , hal_freertos_(hal_freertos)
{
    mutex_ = hal_freertos_.mutex_create();
}

PairingManager::~PairingManager()
{
    deinit();
    if (mutex_)
        hal_freertos_.semaphore_delete(mutex_);
}

esp_err_t PairingManager::init(NodeType type, NodeId id)
{
    my_type_ = type;
    my_id_ = id;
    return ESP_OK;
}

void PairingManager::set_channel(uint8_t channel)
{
    current_channel_ = channel;
}

esp_err_t PairingManager::deinit()
{
    hal_freertos_.semaphore_take(mutex_, PORT_MAX_DELAY);
    if (timeout_timer_) {
        hal_freertos_.timer_delete(timeout_timer_, PORT_MAX_DELAY);
        timeout_timer_ = nullptr;
    }
    if (periodic_timer_) {
        hal_freertos_.timer_delete(periodic_timer_, PORT_MAX_DELAY);
        periodic_timer_ = nullptr;
    }
    is_active_ = false;
    hal_freertos_.semaphore_give(mutex_);
    return ESP_OK;
}

esp_err_t PairingManager::start(uint32_t timeout_ms)
{
    hal_freertos_.semaphore_take(mutex_, PORT_MAX_DELAY);
    if (is_active_) {
        hal_freertos_.semaphore_give(mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Pairing started for %u ms.", (unsigned int)timeout_ms);

    timeout_timer_ = hal_freertos_.timer_create("pair_timeout", pdMS_TO_TICKS(timeout_ms), pdFALSE, this, timeout_cb);
    if (my_type_ != ReservedTypes::HUB) {
        periodic_timer_ = hal_freertos_.timer_create("pair_periodic", pdMS_TO_TICKS(5000), pdTRUE, this, periodic_cb);
        hal_freertos_.timer_start(periodic_timer_, 0);
        send_pair_request();
    }
    hal_freertos_.timer_start(timeout_timer_, 0);
    is_active_ = true;
    hal_freertos_.semaphore_give(mutex_);
    return ESP_OK;
}

void PairingManager::handle_request(const RxPacket &packet)
{
    hal_freertos_.semaphore_take(mutex_, PORT_MAX_DELAY);
    if (!is_active_ || my_type_ != ReservedTypes::HUB) {
        hal_freertos_.semaphore_give(mutex_);
        return;
    }
    hal_freertos_.semaphore_give(mutex_);

    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt)
        return;
    const MessageHeader &header = header_opt.value();
    const PairRequest *req = reinterpret_cast<const PairRequest *>(packet.data);

    ESP_LOGI(TAG, "Pair request from Node ID %d", (int)header.sender_node_id);

    PairResponse resp;
    resp.header.msg_type = MessageType::PAIR_RESPONSE;
    resp.header.sender_node_id = my_id_;
    resp.header.sender_type = my_type_;
    resp.header.dest_node_id = header.sender_node_id;
    resp.header.sequence_number = 0;

    if (header.sender_type == ReservedTypes::HUB) {
        resp.status = PairStatus::REJECTED_NOT_ALLOWED;
    }
    else {
        peer_mgr_.add(header.sender_node_id, packet.src_mac, header.sender_type, req->heartbeat_interval_ms);
        resp.status = PairStatus::ACCEPTED;
        resp.wifi_channel = current_channel_;
    }

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    tx_packet.len = codec_.encode(resp.header, &resp.status, sizeof(PairResponse) - sizeof(MessageHeader), tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len > 0) {
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void PairingManager::handle_response(const RxPacket &packet)
{
    hal_freertos_.semaphore_take(mutex_, PORT_MAX_DELAY);
    if (!is_active_ || my_type_ == ReservedTypes::HUB) {
        hal_freertos_.semaphore_give(mutex_);
        return;
    }

    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt) {
        xSemaphoreGive(mutex_);
        return;
    }

    const PairResponse *resp = reinterpret_cast<const PairResponse *>(packet.data);
    if (resp->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub on channel %d.", (int)resp->wifi_channel);
        peer_mgr_.set_channel(resp->wifi_channel);
        peer_mgr_.add(header_opt->sender_node_id, packet.src_mac, header_opt->sender_type);
        is_active_ = false;
        if (periodic_timer_) {
            hal_freertos_.timer_stop(periodic_timer_, 0);
        }
        if (timeout_timer_) {
            hal_freertos_.timer_stop(timeout_timer_, 0);
        }
    }
    hal_freertos_.semaphore_give(mutex_);
}

void PairingManager::send_pair_request()
{
    PairRequest req;
    req.header.msg_type = MessageType::PAIR_REQUEST;
    req.header.sender_node_id = my_id_;
    req.header.sender_type = my_type_;
    req.header.dest_node_id = ReservedIds::HUB;
    req.header.sequence_number = 0;
    req.heartbeat_interval_ms = 60000;

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, BROADCAST_MAC, 6);

    tx_packet.len = codec_.encode(req.header, &req.firmware_version, sizeof(PairRequest) - sizeof(MessageHeader), tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len > 0) {
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void PairingManager::timeout_cb(TimerHandle_t xTimer)
{
    static_cast<PairingManager *>(pvTimerGetTimerID(xTimer))->on_timeout();
}

void PairingManager::periodic_cb(TimerHandle_t xTimer)
{
    static_cast<PairingManager *>(pvTimerGetTimerID(xTimer))->send_pair_request();
}

void PairingManager::on_timeout()
{
    hal_freertos_.semaphore_take(mutex_, PORT_MAX_DELAY);
    is_active_ = false;
    if (periodic_timer_)
        hal_freertos_.timer_stop(periodic_timer_, 0);
    ESP_LOGI(TAG, "Pairing timed out.");
    hal_freertos_.semaphore_give(mutex_);
}
