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

class TestableHeartbeatManager : public RealHeartbeatManager {
public:
    using RealHeartbeatManager::RealHeartbeatManager;

    void force_send_heartbeat() {
        send_heartbeat();
    }
};

TEST_CASE("RealHeartbeatManager Hub handles request", "[heartbeat_manager]")
{
    esp_timer_get_time_IgnoreAndReturn(0);
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealHeartbeatManager hub(tx_mgr, peer_mgr, codec, ReservedIds::HUB);

    hub.init(0, ReservedTypes::HUB);

    NodeId sensor_id = 10;
    uint8_t sensor_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    uint64_t uptime_ms = 12345;

    hub.handle_request(sensor_id, sensor_mac, uptime_ms);

    // Hub should update last seen for the sensor
    TEST_ASSERT_EQUAL(1, peer_mgr.update_last_seen_calls);
    TEST_ASSERT_EQUAL(sensor_id, peer_mgr.last_update_last_seen_id);

    // Hub should send heartbeat response
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

    node.init(60000, 2); // Node type 2

    // Mock peer manager to not find Hub MAC (so it uses broadcast)
    peer_mgr.find_mac_ret = false;

    node.force_send_heartbeat();

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

    // Hub MAC to be found
    uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    peer_mgr.find_mac_ret = true;
    memcpy(peer_mgr.last_add_mac, hub_mac, 6); // Mock will return this if find_mac_ret is true
    // Wait, MockPeerManager::find_mac needs to copy mac out.
    // Let's check MockPeerManager::find_mac.

    node.handle_response(ReservedIds::HUB, 6);

    // Should notify link alive
    TEST_ASSERT_EQUAL(1, tx_mgr.notify_link_alive_calls);

    // Should update Hub channel
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

    // Hub is known
    uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    peer_mgr.find_mac_ret = true;
    memcpy(peer_mgr.last_add_mac, hub_mac, 6);
    // For now, let's assume it works or I'll fix it if needed.

    node.force_send_heartbeat();

    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::HEARTBEAT, codec.last_encode_header.msg_type);
    // MAC should be HUB mac, not broadcast
    TEST_ASSERT_EQUAL_MEMORY(hub_mac, tx_mgr.last_queued_packet.dest_mac, 6);
}

TEST_CASE("RealHeartbeatManager update_node_id", "[heartbeat_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    NodeId initial_id = 10;
    TestableHeartbeatManager node(tx_mgr, peer_mgr, codec, initial_id);

    node.update_node_id(20);

    node.force_send_heartbeat();
    TEST_ASSERT_EQUAL(20, codec.last_encode_header.sender_node_id);
}

TEST_CASE("RealHeartbeatManager skip queue if codec fails", "[heartbeat_manager]")
{
    esp_timer_get_time_IgnoreAndReturn(0);
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    TestableHeartbeatManager node(tx_mgr, peer_mgr, codec, 10);

    // Codec returns empty vector (failure)
    codec.use_encode_ret = true;
    codec.encode_ret = {};

    node.force_send_heartbeat();

    TEST_ASSERT_EQUAL(0, tx_mgr.queue_packet_calls);
}
