#include "heartbeat_manager.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

static const char *TAG = "HeartbeatMgr";

RealHeartbeatManager::RealHeartbeatManager(ITxManager &tx_mgr,
                                           IPeerManager &peer_mgr,
                                           IMessageCodec &codec,
                                           NodeId my_id)
    : tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , codec_(codec)
    , my_id_(my_id)
{
}

RealHeartbeatManager::~RealHeartbeatManager()
{
    deinit();
}

void RealHeartbeatManager::update_node_id(NodeId id)
{
    my_id_ = id;
}

esp_err_t RealHeartbeatManager::init(uint32_t interval_ms, NodeType type)
{
    interval_ms_ = interval_ms;
    my_type_     = type;

    if (my_type_ != ReservedTypes::HUB && interval_ms_ > 0) {
        timer_ = xTimerCreate("heartbeat", pdMS_TO_TICKS(interval_ms_), pdTRUE, this, timer_cb);
        if (timer_ == nullptr)
            return ESP_FAIL;
        xTimerStart(timer_, 0);
    }
    return ESP_OK;
}

esp_err_t RealHeartbeatManager::deinit()
{
    if (timer_) {
        xTimerStop(timer_, portMAX_DELAY);
        xTimerDelete(timer_, portMAX_DELAY);
        timer_ = nullptr;
    }
    return ESP_OK;
}

void RealHeartbeatManager::handle_response(NodeId hub_id, uint8_t channel)
{
    ESP_LOGI(TAG, "Heartbeat response received from Hub ID %d. Wifi Channel: %d", (int)hub_id, channel);

    // Notify TxManager that the link is alive
    tx_mgr_.notify_link_alive();

    // Update the Hub's channel in PeerManager if we have its MAC
    uint8_t mac[6];
    if (peer_mgr_.find_mac(hub_id, mac)) {
        peer_mgr_.add(hub_id, mac, channel, ReservedTypes::HUB);
    }
}

void RealHeartbeatManager::handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms)
{
    uint64_t now_ms = esp_timer_get_time() / 1000;
    peer_mgr_.update_last_seen(sender_id, now_ms);
    ESP_LOGI(TAG, "Heartbeat received from Node ID %d.", (int)sender_id);

    HeartbeatResponse response;
    response.header.msg_type        = MessageType::HEARTBEAT_RESPONSE;
    response.header.sender_node_id  = my_id_;
    response.header.sender_type     = my_type_;
    response.header.dest_node_id    = sender_id;
    response.header.sequence_number = 0;
    response.server_time_ms         = now_ms;
    response.wifi_channel           = 1; // Needs real channel, but for now fixed

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, mac, 6);
    auto encoded =
        codec_.encode(response.header, &response.server_time_ms, sizeof(HeartbeatResponse) - sizeof(MessageHeader));
    if (!encoded.empty()) {
        tx_packet.len = encoded.size();
        memcpy(tx_packet.data, encoded.data(), tx_packet.len);
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void RealHeartbeatManager::send_heartbeat()
{
    TxPacket tx_packet;
    if (!peer_mgr_.find_mac(ReservedIds::HUB, tx_packet.dest_mac)) {
        const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(tx_packet.dest_mac, broadcast_mac, 6);
    }

    HeartbeatMessage heartbeat;
    heartbeat.header.msg_type        = MessageType::HEARTBEAT;
    heartbeat.header.sender_node_id  = my_id_;
    heartbeat.header.sender_type     = my_type_;
    heartbeat.header.dest_node_id    = ReservedIds::HUB;
    heartbeat.header.sequence_number = 0;
    heartbeat.uptime_ms              = esp_timer_get_time() / 1000;

    auto encoded =
        codec_.encode(heartbeat.header, &heartbeat.battery_mv, sizeof(HeartbeatMessage) - sizeof(MessageHeader));
    if (!encoded.empty()) {
        tx_packet.len = encoded.size();
        memcpy(tx_packet.data, encoded.data(), tx_packet.len);
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void RealHeartbeatManager::timer_cb(TimerHandle_t xTimer)
{
    static_cast<RealHeartbeatManager *>(pvTimerGetTimerID(xTimer))->send_heartbeat();
}
