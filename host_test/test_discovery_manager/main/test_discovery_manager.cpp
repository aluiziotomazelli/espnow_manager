#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "discovery_manager.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_message_codec.hpp"
#include "mock_tx_manager.hpp"
#include "i_channel_observer.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockChannelObserver : public IChannelObserver
{
public:
    MOCK_METHOD(void, on_channel_found_cb, (uint8_t channel), (override));
    MOCK_METHOD(void, on_scan_failed_cb, (), (override));
};

class DiscoveryManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> wifi_hal;
    NiceMock<MockMessageCodec> codec;
    NiceMock<MockFreeRTOSHAL> freertos_hal;
    NiceMock<MockTxManager> tx_manager;
    MockChannelObserver observer;
    std::unique_ptr<DiscoveryManager> scanner;

    static constexpr NodeId MY_ID = 2;
    static constexpr NodeType MY_TYPE = 0x02;
    static constexpr uint8_t VALID_CHANNEL = 6;

    void SetUp() override
    {
        // default happy path
        ON_CALL(wifi_hal, wifi_set_channel(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(3));
        ON_CALL(wifi_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

        // default: notify_wait timeout, hub not found
        ON_CALL(freertos_hal, task_notify_wait(_, _, _, _)).WillByDefault(Return(pdFAIL));

        scanner = std::make_unique<DiscoveryManager>(wifi_hal, codec, freertos_hal);
        scanner->init(MY_ID, MY_TYPE, &tx_manager, &observer);
    }
};

TEST_F(DiscoveryManagerTest, FindHubOnFirstChannel)
{
    scanner->set_channel(VALID_CHANNEL);

    // Verify that when hub is found on the first channel and first attempt,
    // each function is called exactly once
    EXPECT_CALL(wifi_hal, wifi_set_channel(VALID_CHANNEL)).Times(1).WillOnce(Return(ESP_OK)); // set channel
    EXPECT_CALL(wifi_hal, hal_esp_now_send(_, _, _)).Times(1).WillOnce(Return(ESP_OK));       // send probe
    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(
            SetArgPointee<2>(NOTIFY_LINK_ALIVE), // received bits
            Return(pdPASS)));                    // return pdPASS

    EXPECT_CALL(observer, on_channel_found_cb(VALID_CHANNEL)).Times(1);

    IDiscoveryManager::ScanResult res = scanner->scan();
    ASSERT_TRUE(res.hub_found);
    ASSERT_EQ(VALID_CHANNEL, res.channel);
}

TEST_F(DiscoveryManagerTest, ScanSucceedsWithObserver)
{
    scanner->set_channel(VALID_CHANNEL);

    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_LINK_ALIVE), Return(pdPASS)));

    EXPECT_CALL(observer, on_channel_found_cb(VALID_CHANNEL)).Times(1);

    IDiscoveryManager::ScanResult res = scanner->scan();
    ASSERT_TRUE(res.hub_found);
    ASSERT_EQ(VALID_CHANNEL, res.channel);
}

TEST_F(DiscoveryManagerTest, HubNotFoundOnAnyChannelCallsObserverScanFailed)
{
    scanner->set_channel(VALID_CHANNEL);

    uint8_t channels = 13;
    uint8_t call_times = channels * SCAN_CHANNEL_ATTEMPTS;

    // wifi_set_channel is called once for each channel
    EXPECT_CALL(wifi_hal, wifi_set_channel(_)).Times(channels).WillRepeatedly(Return(ESP_OK));

    // In each channel, the probe is sent SCAN_CHANNEL_ATTEMPTS times
    EXPECT_CALL(wifi_hal, hal_esp_now_send(_, _, _)).Times(call_times).WillRepeatedly(Return(ESP_OK));
    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _)).Times(call_times).WillRepeatedly(Return(pdFAIL));

    EXPECT_CALL(observer, on_channel_found_cb(_)).Times(0);
    EXPECT_CALL(observer, on_scan_failed_cb()).Times(1);

    IDiscoveryManager::ScanResult res = scanner->scan();
    ASSERT_FALSE(res.hub_found);
    ASSERT_EQ(VALID_CHANNEL, res.channel);
}

TEST_F(DiscoveryManagerTest, EmptyEncodedMessageDontCallSend)
{
    scanner->set_channel(VALID_CHANNEL);
    ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(0));

    // esp_now_send is not called
    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _)).Times(0);

    IDiscoveryManager::ScanResult res = scanner->scan();
    ASSERT_FALSE(res.hub_found);
    ASSERT_EQ(VALID_CHANNEL, res.channel);
}

TEST_F(DiscoveryManagerTest, NotificationBitIncorrectReturnsHubNotFound)
{
    scanner->set_channel(VALID_CHANNEL);
    ON_CALL(freertos_hal, task_notify_wait(_, _, _, _))
        .WillByDefault(DoAll(
            SetArgPointee<2>(NOTIFY_LOGICAL_ACK), // incorrect bit
            Return(pdPASS)));                     // return pdPASS

    IDiscoveryManager::ScanResult res = scanner->scan();
    ASSERT_FALSE(res.hub_found);
    ASSERT_EQ(VALID_CHANNEL, res.channel);
}

TEST_F(DiscoveryManagerTest, ProbeMessageHasCorrectHeader)
{
    scanner->set_channel(VALID_CHANNEL);
    MessageHeader captured_header;

    NodeId NEW_ID = 3;
    NodeType NEW_TYPE = 0x04;
    scanner->init(NEW_ID, NEW_TYPE, &tx_manager, &observer);

    EXPECT_CALL(codec, encode(_, _, _, _, _))
        .Times(1)
        .WillOnce(DoAll(testing::SaveArg<0>(&captured_header), Return(1)));

    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_LINK_ALIVE), Return(pdPASS)));

    EXPECT_CALL(observer, on_channel_found_cb(_)).Times(1);

    scanner->scan();

    EXPECT_EQ(captured_header.msg_type, MessageType::CHANNEL_SCAN_PROBE);
    EXPECT_EQ(captured_header.sequence_number, 0);
    EXPECT_EQ(captured_header.sender_type, NEW_TYPE);
    EXPECT_EQ(captured_header.sender_node_id, NEW_ID);
    EXPECT_EQ(captured_header.payload_type, 0x00);
    EXPECT_EQ(captured_header.requires_ack, false);
    EXPECT_EQ(captured_header.dest_node_id, ReservedIds::HUB);
    EXPECT_EQ(captured_header.timestamp_ms, 0);
}

TEST_F(DiscoveryManagerTest, ChannelScanOrderStartsFromGivenChannel)
{
    scanner->set_channel(VALID_CHANNEL);
    testing::Sequence s;

    // Expected channels in order, VALID_CHANNEL = 6
    uint8_t expected_channels[] = {6, 7, 8, 9, 10, 11, 12, 13, 1, 2, 3, 4, 5};

    for (uint8_t ch : expected_channels) {
        EXPECT_CALL(wifi_hal, wifi_set_channel(ch)).Times(1).InSequence(s).WillOnce(Return(ESP_OK));
    }
    scanner->scan();
}

TEST_F(DiscoveryManagerTest, HubFoundOnSecondAttemptOfSameChannel)
{
    scanner->set_channel(VALID_CHANNEL);
    InSequence s;

    EXPECT_CALL(wifi_hal, wifi_set_channel(VALID_CHANNEL)).Times(1);

    // First attempt: no notification (hub not found)
    EXPECT_CALL(wifi_hal, hal_esp_now_send(_, _, _)).Times(1);
    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _)).Times(1).WillOnce(Return(pdFAIL));

    // Second attempt: hub found
    EXPECT_CALL(wifi_hal, hal_esp_now_send(_, _, _)).Times(1);
    EXPECT_CALL(freertos_hal, task_notify_wait(_, _, _, _))
        .Times(1)
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_LINK_ALIVE), Return(pdPASS)));

    EXPECT_CALL(observer, on_channel_found_cb(VALID_CHANNEL)).Times(1);

    IDiscoveryManager::ScanResult res = scanner->scan();
    ASSERT_TRUE(res.hub_found);
    ASSERT_EQ(VALID_CHANNEL, res.channel);
}

TEST_F(DiscoveryManagerTest, HandleProbeIgnoresIfTxMgrNull)
{
    // Raw scanner (not initialized)
    DiscoveryManager raw_scanner(wifi_hal, codec, freertos_hal);

    RxPacket packet;
    EXPECT_CALL(tx_manager, queue_packet(_)).Times(0);
    raw_scanner.handle_probe(packet);
}

TEST_F(DiscoveryManagerTest, HandleProbeIgnoresIfNotHub)
{
    // Setup scanner as Node
    scanner->init(MY_ID, 0x02, nullptr, &observer);

    RxPacket packet;
    EXPECT_CALL(tx_manager, queue_packet(_)).Times(0);
    scanner->handle_probe(packet);
}

TEST_F(DiscoveryManagerTest, HandleProbeIgnoresIfHeaderDecodeFails)
{
    // Setup scanner as Hub
    scanner->init(MY_ID, ReservedTypes::HUB, &tx_manager, nullptr);

    RxPacket packet;
    // Simulate decode_header failure by returning std::nullopt
    ON_CALL(codec, decode_header(_, _)).WillByDefault(Return(std::nullopt));

    EXPECT_CALL(tx_manager, queue_packet(_)).Times(0);

    scanner->handle_probe(packet);
}

TEST_F(DiscoveryManagerTest, HandleProbeIgnoresIfEncodeFails)
{
    // Setup scanner as Hub
    scanner->init(MY_ID, ReservedTypes::HUB, &tx_manager, nullptr);

    RxPacket packet;
    packet.len = 10;

    // Simulate decode_header failure by returning a valid header but encode failure by returning 0
    ON_CALL(codec, decode_header(_, _))
        .WillByDefault(Return(MessageHeader{MessageType::CHANNEL_SCAN_PROBE, 0, 0x02, 5, 0x00, false, 0, 0}));

    EXPECT_CALL(codec, encode(_, _, _, _, _)).WillOnce(Return(0));
    EXPECT_CALL(tx_manager, queue_packet(_)).Times(0);

    scanner->handle_probe(packet);
}

TEST_F(DiscoveryManagerTest, HandleProbeSendsResponseIfHub)
{
    // Setup scanner as Hub
    scanner->init(MY_ID, ReservedTypes::HUB, &tx_manager, nullptr);

    RxPacket packet;
    packet.len = 10;
    memset(packet.src_mac, 0xAA, 6);

    // Send a valid header to trigger response
    MessageHeader captured_header;
    ON_CALL(codec, decode_header(_, _))
        .WillByDefault(Return(MessageHeader{MessageType::CHANNEL_SCAN_PROBE, 0, 0x02, 5, 0x00, false, 0, 0}));

    EXPECT_CALL(codec, encode(_, _, _, _, _)).WillOnce(DoAll(testing::SaveArg<0>(&captured_header), Return(2)));
    EXPECT_CALL(tx_manager, queue_packet(_)).Times(1); // Must be called to send response

    scanner->handle_probe(packet);

    EXPECT_EQ(captured_header.msg_type, MessageType::CHANNEL_SCAN_RESPONSE);
    EXPECT_EQ(captured_header.dest_node_id, 5);
}

TEST_F(DiscoveryManagerTest, ScanFailsIfNotInitialized)
{
    DiscoveryManager raw_scanner(wifi_hal, codec, freertos_hal);
    IDiscoveryManager::ScanResult res = raw_scanner.scan();
    ASSERT_FALSE(res.hub_found);
}

TEST_F(DiscoveryManagerTest, InitReturnsErrorIfHubMissingTxMgr)
{
    EXPECT_EQ(ESP_ERR_INVALID_ARG, scanner->init(MY_ID, ReservedTypes::HUB, nullptr, nullptr));
}

TEST_F(DiscoveryManagerTest, InitReturnsErrorIfNodeMissingObserver)
{
    EXPECT_EQ(ESP_ERR_INVALID_ARG, scanner->init(MY_ID, MY_TYPE, nullptr, nullptr));
}
