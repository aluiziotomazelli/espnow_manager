#include <cstring>

#include "esp_log.h"

#include "heartbeat_manager.hpp"

static const char* TAG = "HeartbeatMgr";

HeartbeatManager::HeartbeatManager(ITxManager& tx_mgr, IPeerManager& peer_mgr, ITimerHAL& hal_timer)
    : tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , hal_timer_(hal_timer)
{
}

void HeartbeatManager::init(NodeId id, NodeType type, uint32_t interval_ms)
{
    my_id_ = id;
    my_type_ = type;
    interval_ms_ = interval_ms;
    is_initialized_ = true;
}

void HeartbeatManager::deinit()
{
    is_initialized_ = false;
}

void HeartbeatManager::set_interval_ms(uint32_t heartbeat_interval_ms)
{
    interval_ms_ = heartbeat_interval_ms;
}

void HeartbeatManager::tick(uint64_t now_ms)
{
    if (!is_initialized_ || my_type_ == ReservedTypes::HUB) {
        return;
    }

    if (now_ms - last_heartbeat_ms_ >= interval_ms_) {
        send_heartbeat();
        last_heartbeat_ms_ = now_ms;
    }
}

// TODO: After target tests, exclude this method and MessageRouter
// calling andle_response on HEARTBEAT_RESPONSE case, handle_response useful in target tests because log
void HeartbeatManager::handle_response()
{
    ESP_LOGI(TAG, "Heartbeat response received from Hub");

    // link_alive already notified by rx_task on packet reception
    tx_mgr_.notify_link_alive();
}

void HeartbeatManager::handle_request(const DecodedRxPacket& decoded)
{
    const MessageHeader& header = decoded.header;

    uint64_t now_ms = hal_timer_.get_time_us() / 1000;

    peer_mgr_.update_last_seen(header.sender_node_id, now_ms);
    ESP_LOGI(TAG, "Heartbeat received from Node ID %d.", (int)header.sender_node_id);

    DecodedTxPacket tx_packet{};
    memcpy(tx_packet.dest_mac, decoded.raw.src_mac, 6);

    tx_packet.header.msg_type = MessageType::HEARTBEAT_RESPONSE;
    tx_packet.header.sender_node_id = my_id_;
    tx_packet.header.sender_type = my_type_;
    tx_packet.header.dest_node_id = header.sender_node_id;

    HeartbeatResponse resp{};
    resp.server_time_ms = now_ms;

    // Copy only the payload portion of HeartbeatResponse, skipping the MessageHeader
    // which is already set separately in tx_packet.header above.
    // &resp.server_time_ms points to the first field after the header.
    tx_packet.payload_len = sizeof(HeartbeatResponse) - sizeof(MessageHeader);
    memcpy(tx_packet.payload, &resp.server_time_ms, tx_packet.payload_len);

    tx_mgr_.queue_packet(tx_packet);
}

void HeartbeatManager::send_heartbeat()
{
    DecodedTxPacket tx_packet{};
    if (!peer_mgr_.find_mac(ReservedIds::HUB, tx_packet.dest_mac)) {
        memcpy(tx_packet.dest_mac, BROADCAST_MAC, 6);
    }

    tx_packet.header.msg_type = MessageType::HEARTBEAT;
    tx_packet.header.sender_node_id = my_id_;
    tx_packet.header.sender_type = my_type_;
    tx_packet.header.dest_node_id = ReservedIds::HUB;

    HeartbeatMessage hb{};
    uint64_t uptime = hal_timer_.get_time_us() / 1000;
    hb.uptime_ms = uptime;
    // battery_mv and rssi remain zero until hardware support is added

    // Copy only the payload portion of HeartbeatMessage, skipping MessageHeader.
    // &hb.uptime_ms points to the first field after the header.
    tx_packet.payload_len = sizeof(HeartbeatMessage) - sizeof(MessageHeader);
    memcpy(tx_packet.payload, &hb.uptime_ms, tx_packet.payload_len);

    tx_mgr_.queue_packet(tx_packet);
}
