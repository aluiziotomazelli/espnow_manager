#include "unity.h"
#include "pairing_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_message_codec.hpp"
#include "protocol_messages.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>

TEST_CASE("RealPairingManager init", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    TEST_ASSERT_EQUAL(ESP_OK, pairing.init(ReservedTypes::HUB, ReservedIds::HUB));
}

TEST_CASE("RealPairingManager starts as Node", "[pairing_manager]")
{
    MockTxManager tx_mgr;
    MockPeerManager peer_mgr;
    MockMessageCodec codec;
    RealPairingManager pairing(tx_mgr, peer_mgr, codec);

    pairing.init(2, 10); // Node type 2, ID 10

    // Node should send a pair request on start
    TEST_ASSERT_EQUAL(ESP_OK, pairing.start(1000));
    TEST_ASSERT_TRUE(pairing.is_active());

    // Check if send_pair_request was called (indirectly via tx_mgr.queue_packet)
    TEST_ASSERT_EQUAL(1, tx_mgr.queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::PAIR_REQUEST, codec.last_encode_header.msg_type);

    pairing.deinit();
}

TEST_CASE("MockPairingManager spying and stubbing works", "[pairing_mock]")
{
    MockPairingManager mock;

    mock.start_ret = ESP_FAIL;
    TEST_ASSERT_EQUAL(ESP_FAIL, mock.start(500));
    TEST_ASSERT_EQUAL(1, mock.start_calls);
    TEST_ASSERT_EQUAL(500, mock.last_timeout_ms);

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

    // Hub should NOT send pair requests on start
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

    PairRequest req;
    req.header.msg_type = MessageType::PAIR_REQUEST;
    req.header.sender_node_id = 10;
    req.header.sender_type = 2;
    req.heartbeat_interval_ms = 5000;

    memcpy(packet.data, &req, sizeof(PairRequest));
    packet.len = sizeof(PairRequest);

    // Stub codec to decode header
    codec.decode_header_ret = req.header;

    pairing.handle_request(packet);

    // Hub should add peer
    TEST_ASSERT_EQUAL(1, peer_mgr.add_calls);
    TEST_ASSERT_EQUAL(10, peer_mgr.last_add_id);
    TEST_ASSERT_EQUAL_MEMORY(sensor_mac, peer_mgr.last_add_mac, 6);

    // Hub should send response
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
    pairing.start(1000);
    TEST_ASSERT_TRUE(pairing.is_active());

    RxPacket packet;
    uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    memcpy(packet.src_mac, hub_mac, 6);

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

    // Node should add Hub peer
    TEST_ASSERT_EQUAL(1, peer_mgr.add_calls);
    TEST_ASSERT_EQUAL(1, peer_mgr.last_add_id);
    TEST_ASSERT_EQUAL(6, peer_mgr.last_add_channel);

    // Pairing should become inactive
    TEST_ASSERT_FALSE(pairing.is_active());

    pairing.deinit();
}
