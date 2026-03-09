#include <cstring>
#include <memory>

#include "esp_log.h"
#include "esp_wifi.h"
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

extern "C" {
#include "Mockesp_now.h"
#include "Mockesp_timer.h"
#include "Mockesp_wifi.h"
}

#include "espnow_manager.hpp"
#include "host_test_common.hpp"

/**
 * @file test_espnow_manager_main.cpp
 * @brief Integration tests for EspNowManager manager using Dependency Injection.
 *
 * These tests verify the orchestration logic of the EspNowManager class, ensuring that
 * internal tasks are correctly created, data flows between the dispatch and worker tasks,
 * and the public API interacts correctly with the sub-managers.
 */

// Helper to wait for an asynchronous condition with timeout.
#define WAIT_FOR_CONDITION(cond, timeout_ms)      \
    {                                             \
        int count = 0;                            \
        while (!(cond) && count < (timeout_ms)) { \
            vTaskDelay(pdMS_TO_TICKS(10));        \
            count += 10;                          \
        }                                         \
    }

// Wrapper class to expose internal state for testing.
class TestableEspNow : public EspNowManager
{
public:
    TestableEspNow(std::unique_ptr<IPeerManager> peer_manager,
                   std::unique_ptr<ITxManager> tx_manager,
                   IChannelScanner *scanner_ptr,
                   std::unique_ptr<IMessageCodec> message_codec,
                   std::unique_ptr<IHeartbeatManager> heartbeat_manager,
                   std::unique_ptr<IPairingManager> pairing_manager,
                   std::unique_ptr<IMessageRouter> message_router)
        : EspNowManager(std::move(peer_manager),
                        std::move(tx_manager),
                        scanner_ptr,
                        std::move(message_codec),
                        std::move(heartbeat_manager),
                        std::move(pairing_manager),
                        std::move(message_router))
    {
    }

    QueueHandle_t get_rx_dispatch_queue()
    {
        return rx_dispatch_queue_;
    }
    QueueHandle_t get_transport_worker_queue()
    {
        return transport_worker_queue_;
    }
    std::optional<MessageHeader> get_last_header_requiring_ack()
    {
        return last_header_requiring_ack_;
    }
};

static MockPeerManager *mock_peer;
static MockTxManager *mock_tx;
static MockChannelScanner *mock_scanner;
static MockMessageCodec *mock_codec;
static MockHeartbeatManager *mock_heartbeat;
static MockPairingManager *mock_pairing;
static MockMessageRouter *mock_router;
static TestableEspNow *espnow;
static QueueHandle_t app_queue;

static esp_err_t mocked_get_mode(wifi_mode_t *mode, int calls)
{
    if (mode)
        *mode = WIFI_MODE_STA;
    return ESP_OK;
}

void setUp(void)
{
    Mockesp_wifi_Init();
    Mockesp_now_Init();
    Mockesp_timer_Init();

    // Default expectations for init()
    esp_wifi_get_mode_StubWithCallback(mocked_get_mode);
    esp_now_init_IgnoreAndReturn(ESP_OK);
    esp_now_register_recv_cb_IgnoreAndReturn(ESP_OK);
    esp_now_register_send_cb_IgnoreAndReturn(ESP_OK);
    esp_wifi_set_channel_IgnoreAndReturn(ESP_OK);
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_deinit_IgnoreAndReturn(ESP_OK);
    esp_timer_get_time_IgnoreAndReturn(0);

    mock_peer      = new MockPeerManager();
    mock_tx        = new MockTxManager();
    mock_scanner   = new MockChannelScanner();
    mock_codec     = new MockMessageCodec();
    mock_heartbeat = new MockHeartbeatManager();
    mock_pairing   = new MockPairingManager();
    mock_router    = new MockMessageRouter();

    espnow = new TestableEspNow(
        std::unique_ptr<IPeerManager>(mock_peer), std::unique_ptr<ITxManager>(mock_tx), mock_scanner,
        std::unique_ptr<IMessageCodec>(mock_codec), std::unique_ptr<IHeartbeatManager>(mock_heartbeat),
        std::unique_ptr<IPairingManager>(mock_pairing), std::unique_ptr<IMessageRouter>(mock_router));

    app_queue = xQueueCreate(10, sizeof(RxPacket));
}

void tearDown(void)
{
    if (espnow->is_initialized()) {
        espnow->deinit();
    }
    delete espnow;
    if (app_queue)
        vQueueDelete(app_queue);

    // Small delay to ensure any remaining task cleanup in FreeRTOS (on host)
    vTaskDelay(pdMS_TO_TICKS(50));

    Mockesp_wifi_Verify();
    Mockesp_wifi_Destroy();
    Mockesp_now_Verify();
    Mockesp_now_Destroy();
    Mockesp_timer_Verify();
    Mockesp_timer_Destroy();
}

TEST_CASE("EspNowManager Init/Deinit (Happy Path)", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    config.node_id      = 10;
    config.node_type    = 2;

    // Simulate existing peers in storage
    PeerInfo existing_peer = {};
    existing_peer.node_id  = 55;
    memset(existing_peer.mac, 0xAA, 6);
    existing_peer.channel = 1;
    mock_peer->get_all_ret.push_back(existing_peer);

    // Expecting esp_now_add_peer for the broadcast AND the existing peer
    esp_now_add_peer_StopIgnore();
    esp_now_add_peer_ExpectAnyArgsAndReturn(ESP_OK); // broadcast
    esp_now_add_peer_ExpectAnyArgsAndReturn(ESP_OK); // existing peer

    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));
    TEST_ASSERT_TRUE(espnow->is_initialized());

    // Verify sub-managers were initialized
    TEST_ASSERT_EQUAL(1, mock_tx->init_calls);
    TEST_ASSERT_EQUAL(1, mock_heartbeat->update_node_id_calls);
    TEST_ASSERT_EQUAL(1, mock_pairing->init_calls);
    TEST_ASSERT_EQUAL(1, mock_router->set_app_queue_calls);

    TEST_ASSERT_EQUAL(ESP_OK, espnow->deinit());
    TEST_ASSERT_FALSE(espnow->is_initialized());
}

TEST_CASE("EspNowManager: Transport worker updates channel on HeartbeatResponse", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    config.wifi_channel = 1;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    HeartbeatResponse resp = {};
    resp.header.msg_type   = MessageType::HEARTBEAT_RESPONSE;
    resp.wifi_channel      = 6;

    RxPacket packet;
    packet.len = sizeof(resp);
    memcpy(packet.data, &resp, sizeof(resp));

    mock_codec->validate_crc_ret               = true;
    mock_codec->decode_header_ret              = resp.header;
    mock_router->should_dispatch_to_worker_ret = true;

    // Expect broadcast peer to be modified
    // We rely on Ignore from setUp, but we can verify via persist_calls

    // Inject into dispatch queue -> worker queue
    xQueueSend(espnow->get_rx_dispatch_queue(), &packet, 0);

    // Wait for worker task to process and call persist
    WAIT_FOR_CONDITION(mock_peer->persist_calls >= 1, 1000);

    TEST_ASSERT_EQUAL(1, mock_peer->persist_calls);
    TEST_ASSERT_EQUAL(6, mock_peer->last_persist_wifi_channel);
}

TEST_CASE("EspNowManager: Transport worker updates channel on ChannelScanResponse", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    config.wifi_channel = 1;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    MessageHeader h = {};
    h.msg_type      = MessageType::CHANNEL_SCAN_RESPONSE;

    RxPacket packet;
    packet.len = sizeof(h);
    memcpy(packet.data, &h, sizeof(h));

    mock_codec->validate_crc_ret               = true;
    mock_codec->decode_header_ret              = h;
    mock_router->should_dispatch_to_worker_ret = true;

    // Simulate esp_wifi_get_channel returning 11
    uint8_t mocked_ch = 11;
    esp_wifi_get_channel_ExpectAnyArgsAndReturn(ESP_OK);
    esp_wifi_get_channel_ReturnMemThruPtr_primary(&mocked_ch, sizeof(uint8_t));

    // Expect broadcast peer to be modified
    // We rely on Ignore from setUp

    xQueueSend(espnow->get_rx_dispatch_queue(), &packet, 0);

    WAIT_FOR_CONDITION(mock_peer->persist_calls >= 1, 1000);

    TEST_ASSERT_EQUAL(1, mock_peer->persist_calls);
    TEST_ASSERT_EQUAL(11, mock_peer->last_persist_wifi_channel);
}

TEST_CASE("EspNowManager Init cleans up on partial failure", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;

    // Simulate failure at esp_now_register_recv_cb() which is AFTER esp_now_init()
    esp_now_init_StopIgnore();
    esp_now_init_ExpectAndReturn(ESP_OK);
    esp_now_register_recv_cb_StopIgnore();
    esp_now_register_recv_cb_ExpectAnyArgsAndReturn(ESP_FAIL);

    // Deinit expectations: since esp_now_init succeeded, esp_now_deinit MUST be called.
    esp_now_deinit_StopIgnore();
    esp_now_deinit_ExpectAndReturn(ESP_OK);

    esp_err_t err = espnow->init(config);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
    TEST_ASSERT_FALSE(espnow->is_initialized());

    // Verify queues and mutex are null (cleaned up by deinit)
    TEST_ASSERT_NULL(espnow->get_rx_dispatch_queue());
    TEST_ASSERT_NULL(espnow->get_transport_worker_queue());
}

TEST_CASE("EspNowManager Init rejects invalid config", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = nullptr; // Invalid!

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, espnow->init(config));
}

TEST_CASE("EspNowManager double initialization rejected", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;

    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));
    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, espnow->init(config));
}

TEST_CASE("EspNowManager Rx Pipeline: Packet dispatching", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    // Prepare an incoming packet (simulating radio receive)
    RxPacket packet;
    memset(&packet, 0, sizeof(packet));
    packet.len = sizeof(MessageHeader);

    MessageHeader h  = {};
    h.msg_type       = MessageType::DATA;
    h.sender_node_id = 42;
    memcpy(packet.data, &h, sizeof(h));

    // Mock Codec behavior
    mock_codec->validate_crc_ret  = true;
    mock_codec->decode_header_ret = h;

    // Mock Router: DATA should NOT go to worker task
    mock_router->should_dispatch_to_worker_ret = false;

    // Inject directly into the dispatch queue (simulating esp_now_recv_cb)
    xQueueSend(espnow->get_rx_dispatch_queue(), &packet, 0);

    // Wait for the dispatch task to process the packet and call the router
    WAIT_FOR_CONDITION(mock_router->handle_packet_calls >= 1, 500);

    TEST_ASSERT_EQUAL(1, mock_router->handle_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::DATA, mock_router->last_rx_packet.data[0]); // Check msg_type
}

TEST_CASE("EspNowManager Rx Pipeline: Protocol message goes to worker", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    MessageHeader h = {};
    h.msg_type      = MessageType::HEARTBEAT;

    RxPacket packet;
    packet.len = sizeof(h);
    memcpy(packet.data, &h, sizeof(h));

    mock_codec->validate_crc_ret  = true;
    mock_codec->decode_header_ret = h;

    // Mock Router: HEARTBEAT SHOULD go to worker task
    mock_router->should_dispatch_to_worker_ret = true;

    xQueueSend(espnow->get_rx_dispatch_queue(), &packet, 0);

    // The worker task will process it from the worker queue.
    // Wait for worker task to call handle_packet
    WAIT_FOR_CONDITION(mock_router->handle_packet_calls >= 1, 1000);
    TEST_ASSERT_EQUAL(1, mock_router->handle_packet_calls);
}

TEST_CASE("EspNowManager ACK Mutex: Stores header if requires_ack", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    MessageHeader h   = {};
    h.msg_type        = MessageType::DATA;
    h.requires_ack    = true;
    h.sequence_number = 77;

    RxPacket packet;
    packet.len = sizeof(h);
    memcpy(packet.data, &h, sizeof(h));

    mock_codec->validate_crc_ret               = true;
    mock_codec->decode_header_ret              = h;
    mock_router->should_dispatch_to_worker_ret = false;

    xQueueSend(espnow->get_rx_dispatch_queue(), &packet, 0);

    // Wait for processing
    WAIT_FOR_CONDITION(mock_router->handle_packet_calls >= 1, 500);

    // Verify the manager stored the header for logical ACK confirmation
    auto stored = espnow->get_last_header_requiring_ack();
    TEST_ASSERT_TRUE(stored.has_value());
    TEST_ASSERT_EQUAL(77, stored->sequence_number);
}

TEST_CASE("EspNowManager Public API: send_data calls TxManager", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    uint8_t payload[]       = {1, 2, 3};
    mock_peer->find_mac_ret = true; // Peer must exist

    mock_codec->encode_ret     = {0xAA, 0xBB}; // Dummy encoded
    mock_codec->use_encode_ret = true;

    esp_err_t err = espnow->send_data(20, 1, payload, 3, true);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, mock_tx->queue_packet_calls);
    TEST_ASSERT_TRUE(mock_tx->last_queued_packet.requires_ack);
}

TEST_CASE("EspNowManager Public API: send_command calls TxManager", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    uint8_t payload[]       = {5};
    mock_peer->find_mac_ret = true;

    mock_codec->encode_ret     = {0xCC, 0xDD};
    mock_codec->use_encode_ret = true;

    esp_err_t err = espnow->send_command(30, CommandType::REBOOT, payload, 1, false);

    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_EQUAL(1, mock_tx->queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::COMMAND, mock_codec->last_encode_header.msg_type);
    TEST_ASSERT_EQUAL(static_cast<PayloadType>(CommandType::REBOOT), mock_codec->last_encode_header.payload_type);
}

TEST_CASE("EspNowManager Init: fails if broadcast peer addition fails", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;

    esp_now_init_StopIgnore();
    esp_now_init_ExpectAndReturn(ESP_OK);
    esp_now_register_recv_cb_IgnoreAndReturn(ESP_OK);
    esp_now_register_send_cb_IgnoreAndReturn(ESP_OK);
    esp_wifi_set_channel_IgnoreAndReturn(ESP_OK);

    // Fail the broadcast peer addition
    esp_now_add_peer_StopIgnore();
    esp_now_add_peer_ExpectAnyArgsAndReturn(ESP_FAIL);

    esp_err_t err = espnow->init(config);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}

TEST_CASE("EspNowManager Init: fails if tx_manager init fails", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;

    mock_tx->init_ret = ESP_FAIL;

    esp_err_t err = espnow->init(config);
    TEST_ASSERT_EQUAL(ESP_FAIL, err);
}

TEST_CASE("EspNowManager Public API: confirm_reception sends logical ACK", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, espnow->init(config));

    // 1. Manually set a pending ACK (simulating a received packet with requires_ack=true)
    // We do this by injecting a packet and letting the task process it.
    MessageHeader h                            = {.msg_type        = MessageType::DATA,
                                                  .sequence_number = 5,
                                                  .sender_type     = 2,
                                                  .sender_node_id  = 10,
                                                  .payload_type    = 0,
                                                  .requires_ack    = true,
                                                  .dest_node_id    = 1,
                                                  .timestamp_ms    = 0};
    mock_codec->decode_header_ret              = h;
    mock_codec->validate_crc_ret               = true;
    mock_router->should_dispatch_to_worker_ret = false;

    RxPacket p;
    p.len = sizeof(h);
    memcpy(p.data, &h, sizeof(h));
    xQueueSend(espnow->get_rx_dispatch_queue(), &p, 0);
    WAIT_FOR_CONDITION(espnow->get_last_header_requiring_ack().has_value(), 500);

    // 2. Call confirm_reception
    mock_peer->find_mac_ret = true;
    esp_err_t err           = espnow->confirm_reception(AckStatus::OK);

    TEST_ASSERT_EQUAL(ESP_OK, err);

    // Verify an ACK packet was queued for transmission
    TEST_ASSERT_EQUAL(1, mock_tx->queue_packet_calls);
    TEST_ASSERT_EQUAL(MessageType::ACK, mock_codec->last_encode_header.msg_type);
}
