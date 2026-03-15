#include <cstring>

#include "esp_log.h"

#include "heartbeat_manager.hpp"

static const char *TAG = "HeartbeatMgr";

HeartbeatManager::HeartbeatManager(
    NodeId my_id,
    ITxManager &tx_mgr,
    IPeerManager &peer_mgr,
    IMessageCodec &codec,
    IFreeRTOSHAL &hal_freertos,
    ITimerHAL &hal_timer)
    : my_id_(my_id)
    , tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , codec_(codec)
    , hal_freertos_(hal_freertos)
    , hal_timer_(hal_timer)
{
}

HeartbeatManager::~HeartbeatManager()
{
    deinit();
}

void HeartbeatManager::update_node_id(NodeId id)
{
    my_id_ = id;
}

void HeartbeatManager::set_channel(uint8_t channel)
{
    current_channel_ = channel;
}

esp_err_t HeartbeatManager::init(uint32_t interval_ms, NodeType type)
{
    interval_ms_ = interval_ms;
    my_type_ = type;

    esp_err_t ret = ESP_OK;
    if (my_type_ != ReservedTypes::HUB && interval_ms_ > 0) {
        timer_ = hal_freertos_.timer_create("heartbeat", interval_ms_, pdTRUE, this, timer_cb);
        if (timer_ == nullptr) {
            ret = ESP_FAIL;
        }
        else if (hal_freertos_.timer_start(timer_, pdMS_TO_TICKS(10)) == pdFAIL) {
            ret = ESP_FAIL;
        }
    }
    return ret;
}

esp_err_t HeartbeatManager::deinit()
{
    esp_err_t ret = ESP_OK;
    if (timer_) {
        if (hal_freertos_.timer_stop(timer_, pdMS_TO_TICKS(100)) == pdFAIL) {
            ret = ESP_FAIL;
        }
        else if (hal_freertos_.timer_delete(timer_, pdMS_TO_TICKS(100)) == pdFAIL) {
            ret = ESP_FAIL;
        }
        timer_ = nullptr;
    }
    return ret;
}

void HeartbeatManager::handle_response(NodeId hub_id)
{
    ESP_LOGI(TAG, "Heartbeat response received from Hub ID %d.", (int)hub_id);

    // Notify TxManager that the link is alive
    tx_mgr_.notify_link_alive();
}

void HeartbeatManager::handle_request(const RxPacket &packet)
{
    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt) {
        return;
    }
    const MessageHeader &header = header_opt.value();

    if (packet.len < sizeof(HeartbeatMessage)) {
        ESP_LOGW(TAG, "Malformed HEARTBEAT: len %d < %d", (int)packet.len, (int)sizeof(HeartbeatMessage));
        return;
    }

    // We only need the header for now
    uint64_t now_ms = hal_timer_.get_time_us() / 1000;
    
    peer_mgr_.update_last_seen(header.sender_node_id, now_ms);
    ESP_LOGI(TAG, "Heartbeat received from Node ID %d.", (int)header.sender_node_id);

    HeartbeatResponse response;
    response.header.msg_type = MessageType::HEARTBEAT_RESPONSE;
    response.header.sender_node_id = my_id_;
    response.header.sender_type = my_type_;
    response.header.dest_node_id = header.sender_node_id;
    response.header.sequence_number = 0;
    response.server_time_ms = now_ms;
    response.wifi_channel = current_channel_;

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    tx_packet.len = codec_.encode(response.header, &response.server_time_ms, sizeof(HeartbeatResponse) - sizeof(MessageHeader), tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len > 0) {
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void HeartbeatManager::send_heartbeat()
{
    TxPacket tx_packet;
    if (!peer_mgr_.find_mac(ReservedIds::HUB, tx_packet.dest_mac)) {
        const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(tx_packet.dest_mac, broadcast_mac, 6);
    }

    HeartbeatMessage heartbeat;
    heartbeat.header.msg_type = MessageType::HEARTBEAT;
    heartbeat.header.sender_node_id = my_id_;
    heartbeat.header.sender_type = my_type_;
    heartbeat.header.dest_node_id = ReservedIds::HUB;
    heartbeat.header.sequence_number = 0;
    heartbeat.uptime_ms = hal_timer_.get_time_us() / 1000;

    tx_packet.len = codec_.encode(heartbeat.header, &heartbeat.battery_mv, sizeof(HeartbeatMessage) - sizeof(MessageHeader), tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len > 0) {
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void HeartbeatManager::timer_cb(TimerHandle_t xTimer)
{
    static_cast<HeartbeatManager *>(pvTimerGetTimerID(xTimer))->send_heartbeat();
}
