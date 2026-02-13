#include "unity.h"
#include "heartbeat_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_message_codec.hpp"
#include "protocol_messages.hpp"
extern "C" {
#include "Mockesp_timer.h"
}
#include <cstring>

/**
 * @file test_heartbeat_manager.cpp
 * @brief Unit tests for the RealHeartbeatManager class.
 *
 * The Heartbeat Manager ensures the link health by periodically sending messages
 * between Nodes and the Hub. It also tracks peer presence.
 */

// Inherit to expose protected methods for high-fidelity testing
class TestableHeartbeatManager : public RealHeartbeatManager {
public:
    using RealHeartbeatManager::RealHeartbeatManager;

    // Manually trigger a heartbeat send operation.
    void force_send_heartbeat() {
        send_heartbeat();
    }
};

TEST_CASE("RealHeartbeatManager Hub handles request", "[heartbeat_manager]")
{
    // Stub the ESP-IDF timer function.
    esp_timer_get_time_IgnoreAndReturn(0);

    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealHeartbeatManager hub(tx_mgr, peer_mgr, codec, ReservedIds::HUB);

    // Hub doesn't need a periodic interval.
    hub.init(0, ReservedTypes::HUB);

    NodeId sensor_id = 10;
    uint8_t sensor_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint64_t uptime_ms = 12345;

    // When a Hub receives a Heartbeat request from a sensor:
    hub.handle_request(sensor_id, sensor_mac, uptime_ms);

    // 1. It should update the "last seen" timestamp for that sensor.
    TEST_ASSERT_EQUAL(1, peer_mgr.update_last_seen_calls);
    TEST_ASSERT_EQUAL(sensor_id, peer_mgr.last_update_last_seen_id);

    // 2. It should immediately send back a Heartbeat Response.
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::HEARTBEAT_RESPONSE, codec.last_encode_header.msg_type);
}

TEST_CASE("RealHeartbeatManager Node sends heartbeat", "[heartbeat_manager]")
{
    esp_timer_get_time_IgnoreAndReturn(0);
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    NodeId my_id = 10;
    TestableHeartbeatManager node(tx_mgr, peer_mgr, codec, my_id);

    node.init(60000, 2); // Sensor type

    // Scenario: The Hub MAC is unknown (haven't received any message yet).
    peer_mgr.find_mac_ret = false;

    node.force_send_heartbeat();

    // Verify: The Heartbeat should be sent as a Broadcast.
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::HEARTBEAT, codec.last_encode_header.msg_type);
    TEST_ASSERT_EQUAL(my_id, codec.last_encode_header.sender_node_id);
    TEST_ASSERT_EQUAL(ReservedIds::HUB, codec.last_encode_header.dest_node_id);
}

TEST_CASE("RealHeartbeatManager Node handles response", "[heartbeat_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealHeartbeatManager node(tx_mgr, peer_mgr, codec, 10);

    node.init(60000, 2);

    // Scenario: Node receives a Heartbeat Response from the Hub on a specific channel.
    uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    peer_mgr.find_mac_ret = true;
    memcpy(peer_mgr.last_add_mac, hub_mac, 6);

    node.handle_response(ReservedIds::HUB, 6);

    // Verify: The Node notifies the TxManager that the link is alive (resetting fail counters).
    TEST_ASSERT_EQUAL(1, tx_mgr.notify_link_alive_calls);

    // Verify: The Hub's channel is updated in the peer list.
    TEST_ASSERT_EQUAL(1, peer_mgr.add_calls);
    TEST_ASSERT_EQUAL(ReservedIds::HUB, peer_mgr.last_add_id);
    TEST_ASSERT_EQUAL(6, peer_mgr.last_add_channel);
}

TEST_CASE("RealHeartbeatManager Node sends unicast heartbeat to known Hub", "[heartbeat_manager]")
{
    esp_timer_get_time_IgnoreAndReturn(0);
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    NodeId my_id = 10;
    TestableHeartbeatManager node(tx_mgr, peer_mgr, codec, my_id);

    node.init(60000, 2);

    // Hub is already known (MAC exists in peer list).
    uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    peer_mgr.find_mac_ret = true;
    memcpy(peer_mgr.last_add_mac, hub_mac, 6);

    node.force_send_heartbeat();

    // Verify: The Heartbeat should be sent directly to the Hub's MAC (Unicast).
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::HEARTBEAT, codec.last_encode_header.msg_type);
    TEST_ASSERT_EQUAL_MEMORY(hub_mac, tx_mgr.last_queued_packet.dest_mac, 6);
}

TEST_CASE("RealHeartbeatManager update_node_id", "[heartbeat_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    NodeId initial_id = 10;
    TestableHeartbeatManager node(tx_mgr, peer_mgr, codec, initial_id);

    // Changing the Node ID (e.g. after a pairing assignment).
    node.update_node_id(20);

    node.force_send_heartbeat();

    // Verify: Subsequent heartbeats use the new ID.
    TEST_ASSERT_EQUAL(20, codec.last_encode_header.sender_node_id);
}

TEST_CASE("RealHeartbeatManager skip queue if codec fails", "[heartbeat_manager]")
{
    esp_timer_get_time_IgnoreAndReturn(0);
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    TestableHeartbeatManager node(tx_mgr, peer_mgr, codec, 10);

    // Simulate a codec failure (e.g. out of memory).
    codec.use_encode_ret = true;
    codec.encode_ret = {};

    node.force_send_heartbeat();

    // Verify: Nothing is queued if the packet couldn't be encoded.
    TEST_ASSERT_EQUAL(0, tx_mgr.queue_packet_calls);
}
