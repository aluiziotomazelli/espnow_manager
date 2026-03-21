#include <cstring>

#include "esp_log.h"

#include "heartbeat_manager.hpp"

static const char *TAG = "HeartbeatMgr";

HeartbeatManager::HeartbeatManager(
    NodeId my_id,
    ITxManager &tx_mgr,
    IPeerManager &peer_mgr,
    IFreeRTOSHAL &hal_freertos,
    ITimerHAL &hal_timer)
    : my_id_(my_id)
    , tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
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
    if (timer_ != nullptr) {
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

void HeartbeatManager::handle_request(const DecodedPacket &decoded)
{
    const MessageHeader &header = decoded.header;

    if (decoded.raw.len < sizeof(HeartbeatMessage)) {
        ESP_LOGW(TAG, "Malformed HEARTBEAT: len %d < %d", (int)decoded.raw.len, (int)sizeof(HeartbeatMessage));
        return;
    }

    uint64_t now_ms = hal_timer_.get_time_us() / 1000;

    peer_mgr_.update_last_seen(header.sender_node_id, now_ms);
    ESP_LOGI(TAG, "Heartbeat received from Node ID %d.", (int)header.sender_node_id);

    DecodedTxPacket tx_packet;
    memcpy(tx_packet.dest_mac, decoded.raw.src_mac, 6);

    tx_packet.header.msg_type = MessageType::HEARTBEAT_RESPONSE;
    tx_packet.header.sender_node_id = my_id_;
    tx_packet.header.sender_type = my_type_;
    tx_packet.header.dest_node_id = header.sender_node_id;
    tx_packet.header.sequence_number = 0;
    tx_packet.header.requires_ack = false;
    tx_packet.header.payload_type = 0;
    tx_packet.header.timestamp_ms = 0;

    // HeartbeatResponse layout: uint64_t server_time_ms (8), uint8_t wifi_channel (1)
    tx_packet.payload_len = sizeof(HeartbeatResponse) - sizeof(MessageHeader);
    memcpy(tx_packet.payload, &now_ms, 8);
    tx_packet.payload[8] = current_channel_;

    tx_mgr_.queue_packet(tx_packet);
}

void HeartbeatManager::send_heartbeat()
{
    DecodedTxPacket tx_packet;
    if (!peer_mgr_.find_mac(ReservedIds::HUB, tx_packet.dest_mac)) {
        memcpy(tx_packet.dest_mac, BROADCAST_MAC, 6);
    }

    tx_packet.header.msg_type = MessageType::HEARTBEAT;
    tx_packet.header.sender_node_id = my_id_;
    tx_packet.header.sender_type = my_type_;
    tx_packet.header.dest_node_id = ReservedIds::HUB;
    tx_packet.header.sequence_number = 0;
    tx_packet.header.requires_ack = false;
    tx_packet.header.payload_type = 0;
    tx_packet.header.timestamp_ms = 0;

    uint64_t uptime = hal_timer_.get_time_us() / 1000;

    // HeartbeatMessage layout: uint16_t battery_mv (2), int8_t rssi (1), uint64_t uptime_ms (8)
    tx_packet.payload_len = sizeof(HeartbeatMessage) - sizeof(MessageHeader);
    memset(tx_packet.payload, 0, tx_packet.payload_len);
    // battery_mv and rssi are 0 for now
    memcpy(tx_packet.payload + 2 + 1, &uptime, 8);

    tx_mgr_.queue_packet(tx_packet);
}

void HeartbeatManager::timer_cb(TimerHandle_t xTimer)
{
    static_cast<HeartbeatManager *>(pvTimerGetTimerID(xTimer))->send_heartbeat();
}
