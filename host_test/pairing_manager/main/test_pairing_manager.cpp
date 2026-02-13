#include "unity.h"
#include "pairing_manager.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_message_codec.hpp"
#include "protocol_messages.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

/**
 * @file test_pairing_manager.cpp
 * @brief Unit tests for the RealPairingManager class.
 *
 * Pairing is the process where a Node finds a Hub and establishes a shared link.
 * The manager handles both Hub-side (accept/reject) and Node-side (request/periodic) logic.
 */

// Inherit to expose protected methods for high-fidelity testing
class TestablePairingManager : public RealPairingManager
{
public:
    using RealPairingManager::RealPairingManager;
    // Expose internal callbacks for manual triggering in tests
    void force_timeout() { on_timeout(); }
    void force_periodic_send() { send_pair_request(); }
};

TEST_CASE("RealPairingManager init", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    // Test basic initialization
    TEST_ASSERT_EQUAL(ESP_OK, pairing.init(ReservedTypes::HUB, ReservedIds::HUB));
}

TEST_CASE("RealPairingManager starts as Node", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(2, 10); // Configure as a regular Node (Type 2, ID 10)

    // A regular Node should immediately send a pair request upon starting.
    TEST_ASSERT_EQUAL(ESP_OK, pairing.start(1000));
    TEST_ASSERT_TRUE(pairing.is_active());

    // Verify that the PAIR_REQUEST was encoded and queued for transmission.
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::PAIR_REQUEST, codec.last_encode_header.msg_type);

    pairing.deinit();
}

TEST_CASE("RealPairingManager Hub rejects pairing from another HUB", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    // Configure as HUB
    pairing.init(ReservedTypes::HUB, ReservedIds::HUB);
    pairing.start(1000); // Enable pairing window

    // Simulate a PAIR_REQUEST coming from ANOTHER Hub (invalid scenario)
    RxPacket packet;
    PairRequest req;
    req.header.msg_type = MessageType::PAIR_REQUEST;
    req.header.sender_node_id = 2;
    req.header.sender_type = ReservedTypes::HUB;

    memcpy(packet.data, &req, sizeof(PairRequest));
    packet.len = sizeof(PairRequest);
    codec.decode_header_ret = req.header;

    pairing.handle_request(packet);

    // Verify: The peer should NOT be added.
    TEST_ASSERT_EQUAL(0, peer_mgr.add_calls);

    // Verify: A response should still be sent.
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);

    // Verify: The encoded status should be REJECTED_NOT_ALLOWED.
    TEST_ASSERT_EQUAL(1, codec.encode_calls);
    PairStatus* status = (PairStatus*)codec.last_encode_payload.data();
    TEST_ASSERT_EQUAL(PairStatus::REJECTED_NOT_ALLOWED, *status);

    pairing.deinit();
}

TEST_CASE("RealPairingManager Node state cleanup after timeout", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    TestablePairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(2, 10);
    pairing.start(1000);
    TEST_ASSERT_TRUE(pairing.is_active());

    // Simulate the expiration of the pairing window timer.
    pairing.force_timeout();

    // Verify that the manager stopped the pairing process.
    TEST_ASSERT_FALSE(pairing.is_active());
}

TEST_CASE("RealPairingManager Node periodic send", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    TestablePairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(2, 10);
    pairing.start(1000); // Triggers the 1st request
    int initial_calls = tx_mgr.queue_packet_calls;

    // Simulate the periodic timer firing while still active.
    pairing.force_periodic_send(); // Triggers the 2nd request

    // Verify that a new packet was sent.
    TEST_ASSERT_EQUAL(initial_calls + 1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::PAIR_REQUEST, codec.last_encode_header.msg_type);
}

TEST_CASE("MockPairingManager spying and stubbing works", "[pairing_mock]")
{
    // Verification of the Mock class functionality for other component tests.
    MockPairingManager mock;

    // Stubbing a return value
    mock.start_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mock.start(500));

    // Spying on calls and arguments
    TEST_ASSERT_EQUAL(1, mock.start_calls);
    TEST_ASSERT_EQUAL(500, mock.last_timeout_ms);

    // Testing the convenience struct overlay for captured packets
    RxPacket req_packet;
    req_packet.len = sizeof(PairRequest);
    PairRequest* req = (PairRequest*)req_packet.data;
    req->header.sender_node_id = 42;

    mock.handle_request(req_packet);
    TEST_ASSERT_EQUAL(1, mock.handle_request_calls);
    TEST_ASSERT_EQUAL(42, mock.last_request_data.header.sender_node_id);

    mock.reset();
    TEST_ASSERT_EQUAL(0, mock.start_calls);
    TEST_ASSERT_EQUAL(ESP_OK, mock.start_ret);
}

TEST_CASE("RealPairingManager starts as Hub", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(ReservedTypes::HUB, ReservedIds::HUB);

    // A HUB should NOT send pair requests on start; it should just listen.
    TEST_ASSERT_EQUAL(ESP_OK, pairing.start(1000));
    TEST_ASSERT_TRUE(pairing.is_active());
    TEST_ASSERT_EQUAL(0, tx_mgr.queue_packet_calls);

    pairing.deinit();
}

TEST_CASE("RealPairingManager Hub handles request", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(ReservedTypes::HUB, ReservedIds::HUB);
    pairing.start(1000);

    RxPacket packet;
    uint8_t sensor_mac[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    memcpy(packet.src_mac, sensor_mac, 6);

    // Prepare a valid PAIR_REQUEST from a sensor
    PairRequest req;
    req.header.msg_type = MessageType::PAIR_REQUEST;
    req.header.sender_node_id = 10;
    req.header.sender_type = 2;
    req.heartbeat_interval_ms = 5000;

    memcpy(packet.data, &req, sizeof(PairRequest));
    packet.len = sizeof(PairRequest);

    // Stub codec to recognize the header
    codec.decode_header_ret = req.header;

    pairing.handle_request(packet);

    // Verify: The Hub should automatically add the sensor as a peer.
    TEST_ASSERT_EQUAL(1, peer_mgr.add_calls);
    TEST_ASSERT_EQUAL(10, peer_mgr.last_add_id);
    TEST_ASSERT_EQUAL_MEMORY(sensor_mac, peer_mgr.last_add_mac, 6);

    // Verify: The Hub should send a PAIR_RESPONSE.
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::PAIR_RESPONSE, codec.last_encode_header.msg_type);

    pairing.deinit();
}

TEST_CASE("RealPairingManager Node handles response", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(2, 10);
    pairing.start(1000); // Node is now waiting for a response
    TEST_ASSERT_TRUE(pairing.is_active());

    RxPacket packet;
    uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(packet.src_mac, hub_mac, 6);

    // Prepare a PAIR_RESPONSE from the Hub
    PairResponse resp;
    resp.header.msg_type = MessageType::PAIR_RESPONSE;
    resp.header.sender_node_id = 1;
    resp.header.sender_type = ReservedTypes::HUB;
    resp.status = PairStatus::ACCEPTED;
    resp.wifi_channel = 6;

    memcpy(packet.data, &resp, sizeof(PairResponse));
    packet.len = sizeof(PairResponse);

    codec.decode_header_ret = resp.header;

    pairing.handle_response(packet);

    // Verify: The Node should add the Hub to its peer list using the provided channel.
    TEST_ASSERT_EQUAL(1, peer_mgr.add_calls);
    TEST_ASSERT_EQUAL(1, peer_mgr.last_add_id);
    TEST_ASSERT_EQUAL(6, peer_mgr.last_add_channel);

    // Verify: Pairing is finished, so the manager should become inactive.
    TEST_ASSERT_FALSE(pairing.is_active());

    pairing.deinit();
}
