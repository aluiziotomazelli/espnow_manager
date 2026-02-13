#include "unity.h"
#include "message_router.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_message_codec.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <cstring>

static MockPeerManager mock_peer;
static MockTxManager mock_tx;
static MockHeartbeatManager mock_heartbeat;
static MockPairingManager mock_pairing;
static MockMessageCodec mock_codec;
static RealMessageRouter* router;

void setUp(void)
{
    mock_peer.reset();
    mock_tx.reset();
    mock_heartbeat.reset();
    mock_pairing.reset();
    mock_codec.reset();
    router = new RealMessageRouter(mock_peer, mock_tx, mock_heartbeat, mock_pairing, mock_codec);
}

void tearDown(void)
{
    delete router;
}

// Helper to create a basic RxPacket
static RxPacket create_packet(MessageType type, size_t len)
{
    RxPacket p;
    memset(&p, 0, sizeof(p));
    p.len = len;
    MessageHeader* h = (MessageHeader*)p.data;
    h->msg_type = type;
    h->sender_node_id = 10;
    h->sender_type = 2; // SENSOR
    return p;
}

TEST_CASE("Router dispatches PAIR_REQUEST to PairingManager", "[router]")
{
    RxPacket p = create_packet(MessageType::PAIR_REQUEST, sizeof(PairRequest));

    MessageHeader h;
    h.msg_type = MessageType::PAIR_REQUEST;
    mock_codec.decode_header_ret = h;

    router->handle_packet(p);

    TEST_ASSERT_EQUAL(1, mock_pairing.handle_request_calls);
    TEST_ASSERT_EQUAL(1, mock_tx.notify_link_alive_calls);
}

TEST_CASE("Router dispatches HEARTBEAT to HeartbeatManager", "[router]")
{
    RxPacket p = create_packet(MessageType::HEARTBEAT, sizeof(HeartbeatMessage));
    HeartbeatMessage* msg = (HeartbeatMessage*)p.data;
    msg->uptime_ms = 12345;

    MessageHeader h;
    h.msg_type = MessageType::HEARTBEAT;
    h.sender_node_id = 10;
    mock_codec.decode_header_ret = h;

    router->handle_packet(p);

    TEST_ASSERT_EQUAL(1, mock_heartbeat.handle_request_calls);
    TEST_ASSERT_EQUAL(10, mock_heartbeat.last_sender_id);
    TEST_ASSERT_EQUAL(12345, mock_heartbeat.last_uptime_ms);
}

TEST_CASE("Router handles malformed packets gracefully (Fuzzing)", "[router]")
{
    // Case 1: Header too small (codec fails)
    RxPacket p1 = create_packet(MessageType::HEARTBEAT, 5);
    mock_codec.decode_header_ret = std::nullopt;
    router->handle_packet(p1);
    TEST_ASSERT_EQUAL(0, mock_heartbeat.handle_request_calls);

    // Case 2: Header OK but payload too small for HEARTBEAT
    RxPacket p2 = create_packet(MessageType::HEARTBEAT, sizeof(MessageHeader) + 1);
    MessageHeader h;
    h.msg_type = MessageType::HEARTBEAT;
    mock_codec.decode_header_ret = h;

    router->handle_packet(p2);
    TEST_ASSERT_EQUAL(0, mock_heartbeat.handle_request_calls);

    // Case 3: Payload too small for PAIR_REQUEST
    RxPacket p3 = create_packet(MessageType::PAIR_REQUEST, sizeof(MessageHeader) + 2);
    h.msg_type = MessageType::PAIR_REQUEST;
    mock_codec.decode_header_ret = h;
    router->handle_packet(p3);
    TEST_ASSERT_EQUAL(0, mock_pairing.handle_request_calls);
}

TEST_CASE("Router dispatches DATA to app queue", "[router]")
{
    QueueHandle_t q = xQueueCreate(5, sizeof(RxPacket));
    router->set_app_queue(q);

    RxPacket p = create_packet(MessageType::DATA, sizeof(MessageHeader) + 10);
    MessageHeader h;
    h.msg_type = MessageType::DATA;
    mock_codec.decode_header_ret = h;

    router->handle_packet(p);

    RxPacket received;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(q, &received, 0));
    TEST_ASSERT_EQUAL(p.len, received.len);

    vQueueDelete(q);
}

TEST_CASE("Router handles app queue full (Flood simulation)", "[router]")
{
    QueueHandle_t q = xQueueCreate(2, sizeof(RxPacket));
    router->set_app_queue(q);

    MessageHeader h;
    h.msg_type = MessageType::DATA;
    mock_codec.decode_header_ret = h;

    RxPacket p = create_packet(MessageType::DATA, 20);

    // Send 3 packets to a queue of size 2
    router->handle_packet(p);
    router->handle_packet(p);
    router->handle_packet(p); // This one should be dropped gracefully

    TEST_ASSERT_EQUAL(2, uxQueueMessagesWaiting(q));

    vQueueDelete(q);
}

TEST_CASE("Router handles CHANNEL_SCAN_PROBE as Hub", "[router]")
{
    router->set_node_info(ReservedIds::HUB, ReservedTypes::HUB);

    RxPacket p = create_packet(MessageType::CHANNEL_SCAN_PROBE, sizeof(MessageHeader));
    const uint8_t src_mac[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    memcpy(p.src_mac, src_mac, 6);

    MessageHeader h;
    h.msg_type = MessageType::CHANNEL_SCAN_PROBE;
    h.sender_node_id = 10;
    mock_codec.decode_header_ret = h;

    // Mock encoding of response
    std::vector<uint8_t> dummy_encoded(sizeof(MessageHeader));
    mock_codec.encode_ret = dummy_encoded;
    mock_codec.use_encode_ret = true;

    router->handle_packet(p);

    TEST_ASSERT_EQUAL(1, mock_tx.queue_packet_calls);
    TEST_ASSERT_EQUAL_MEMORY(src_mac, mock_tx.last_queued_packet.dest_mac, 6);
    TEST_ASSERT_EQUAL(MessageType::CHANNEL_SCAN_RESPONSE, mock_codec.last_encode_header.msg_type);
}

TEST_CASE("Router handles interleaved messages (Race condition)", "[router]")
{
    MessageHeader h_hb;
    h_hb.msg_type = MessageType::HEARTBEAT_RESPONSE;

    MessageHeader h_pr;
    h_pr.msg_type = MessageType::PAIR_RESPONSE;

    RxPacket p_hb = create_packet(MessageType::HEARTBEAT_RESPONSE, sizeof(HeartbeatResponse));
    RxPacket p_pr = create_packet(MessageType::PAIR_RESPONSE, sizeof(PairResponse));

    // HB Response
    mock_codec.decode_header_ret = h_hb;
    router->handle_packet(p_hb);
    TEST_ASSERT_EQUAL(1, mock_heartbeat.handle_response_calls);

    // PR Response
    mock_codec.decode_header_ret = h_pr;
    router->handle_packet(p_pr);
    TEST_ASSERT_EQUAL(1, mock_pairing.handle_response_calls);
}

TEST_CASE("Router dispatches ACK to TxManager", "[router]")
{
    RxPacket p = create_packet(MessageType::ACK, sizeof(MessageHeader));
    MessageHeader h;
    h.msg_type = MessageType::ACK;
    mock_codec.decode_header_ret = h;

    router->handle_packet(p);
    TEST_ASSERT_EQUAL(1, mock_tx.notify_logical_ack_calls);
}
