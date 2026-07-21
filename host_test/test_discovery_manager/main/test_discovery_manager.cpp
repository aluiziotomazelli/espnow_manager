// host_test/test_discovery_manager/main/test_discovery_manager.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_en_hal_freertos.hpp"
#include "mock_en_hal_wifi.hpp"
#include "mock_en_hal_espnow.hpp"
#include "mock_message_codec.hpp"

#include "discovery_manager.hpp"
using namespace espnow;

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::Sequence;
using ::testing::SetArgPointee;
using ::testing::StrEq;

/**
 * @brief Testable subclass to expose protected/private methods for unit testing.
 */
class TestableDiscoveryManager : public DiscoveryManager
{
public:
    using DiscoveryManager::DiscoveryManager;
    using DiscoveryManager::hub_was_found;
    using DiscoveryManager::make_probe_header;
    using DiscoveryManager::make_response_header;
    using DiscoveryManager::scan_channel;
    using DiscoveryManager::send_scan_probe;
    using DiscoveryManager::send_scan_response;
    using DiscoveryManager::should_stop_scan;
};

class DiscoveryManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> wifi_hal;
    NiceMock<MockEspNowHAL> espnow_hal;
    NiceMock<MockMessageCodec> codec;
    NiceMock<MockFreeRTOSHAL> freertos_hal;

    std::unique_ptr<TestableDiscoveryManager> scanner;

    static constexpr NodeId MY_ID = 2;
    static constexpr NodeType MY_TYPE = 0x02;
    static constexpr uint8_t VALID_CHANNEL = 6;
    TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(0x6);
    TaskHandle_t fake_discovery_task = reinterpret_cast<TaskHandle_t>(0x7);
    static constexpr uint32_t stack_size = 2048;
    static constexpr UBaseType_t priority = 5;

    void SetUp() override
    {
        // Default happy path for task creation
        ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(fake_discovery_task), Return(pdPASS)));

        scanner = std::make_unique<TestableDiscoveryManager>(wifi_hal, espnow_hal, codec, freertos_hal);
    }

    void init_node() { scanner->init(MY_ID, MY_TYPE, fake_rx_task, priority, stack_size); }
    void init_hub() { scanner->init(MY_ID, ReservedTypes::HUB, fake_rx_task, priority, stack_size); }
    void set_task_handle_null()
    {
        ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(nullptr), Return(pdPASS)));
    }

    DecodedRxPacket make_decoded_packet(NodeId sender_id)
    {
        DecodedRxPacket decoded{};
        decoded.header.sender_node_id = sender_id;
        decoded.header.dest_node_id = MY_ID;
        return decoded;
    }
};

// ... existing test cases ...

TEST_F(DiscoveryManagerTest, HandleScanResponseIgnoresMismatchedDestId)
{
    init_node();
    DecodedRxPacket decoded = make_decoded_packet(10);
    decoded.header.dest_node_id = MY_ID + 1; // Mismatched ID

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_LINK_ALIVE, eSetBits)).Times(0);
    scanner->handle_scan_response(decoded);
}

// =============================================================================
// Initialization & Deinitialization
// =============================================================================

TEST_F(DiscoveryManagerTest, InitCreatesTask)
{
    init_node();
    TestableDiscoveryManager mgr(wifi_hal, espnow_hal, codec, freertos_hal);
    EXPECT_CALL(freertos_hal, task_create(_, StrEq("discovery_task"), 4096, &mgr, 10, _))
        .WillOnce(DoAll(SetArgPointee<5>(fake_discovery_task), Return(pdPASS)));

    EXPECT_EQ(ESP_OK, mgr.init(MY_ID, MY_TYPE, fake_rx_task, 10, 4096));
}

TEST_F(DiscoveryManagerTest, InitFailsIfTaskCreateFails)
{
    init_node();
    TestableDiscoveryManager mgr(wifi_hal, espnow_hal, codec, freertos_hal);
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).WillOnce(Return(pdFAIL));

    EXPECT_EQ(ESP_ERR_NO_MEM, mgr.init(MY_ID, MY_TYPE, fake_rx_task, priority, stack_size));
}

TEST_F(DiscoveryManagerTest, InitFailsIfRxTaskHandleIsNull)
{
    init_node();
    TestableDiscoveryManager mgr(wifi_hal, espnow_hal, codec, freertos_hal);
    EXPECT_EQ(ESP_ERR_INVALID_ARG, mgr.init(MY_ID, MY_TYPE, nullptr, priority, stack_size));
}

TEST_F(DiscoveryManagerTest, DeinitNotifyTaskToStopScan)
{
    init_node();
    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_TASK_TO_STOP | NOTIFY_STOP_SCAN, eSetBits))
        .WillOnce(Return(pdPASS));
    scanner->deinit();
}

TEST_F(DiscoveryManagerTest, DeinitWaitsForTaskToFinish)
{
    init_node();
    using ::testing::AtLeast;
    EXPECT_CALL(freertos_hal, task_delay(_)).Times(AtLeast(1));
    scanner->deinit();
}

TEST_F(DiscoveryManagerTest, DeinitSuspendAndDeleteTask)
{
    init_node();
    EXPECT_CALL(freertos_hal, task_suspend(fake_discovery_task));
    EXPECT_CALL(freertos_hal, task_delete(fake_discovery_task));
    scanner->deinit();
}

TEST_F(DiscoveryManagerTest, DeinitDoesNotCleanUpTaskIfTaskHandleIsNull)
{
    set_task_handle_null();

    init_node();

    EXPECT_CALL(freertos_hal, task_notify(_, _, _)).Times(0);
    EXPECT_CALL(freertos_hal, task_delay(_)).Times(0);
    EXPECT_CALL(freertos_hal, task_suspend(_)).Times(0);
    EXPECT_CALL(freertos_hal, task_delete(_)).Times(0);

    scanner->deinit();
}

// =============================================================================
// Task Communication (Notifications)
// =============================================================================

TEST_F(DiscoveryManagerTest, StartScanNotifiesTask)
{
    init_node();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_START_SCAN, eSetBits)).WillOnce(Return(pdPASS));
    scanner->start_scan();
}

TEST_F(DiscoveryManagerTest, StartScanReturnsIfNotNode)
{
    init_hub();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_START_SCAN, eSetBits)).Times(0);
    scanner->start_scan();
}

TEST_F(DiscoveryManagerTest, StartScanReturnsIfTaskHandleIsNull)
{
    set_task_handle_null();
    init_node();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_START_SCAN, eSetBits)).Times(0);
    scanner->start_scan();
}

TEST_F(DiscoveryManagerTest, StopScanNotifiesTask)
{
    init_node();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_STOP_SCAN, eSetBits)).WillOnce(Return(pdPASS));
    scanner->stop_scan();
}

TEST_F(DiscoveryManagerTest, StopScanReturnsIfNotNode)
{
    init_hub();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_STOP_SCAN, eSetBits)).Times(0);
    scanner->stop_scan();
}

TEST_F(DiscoveryManagerTest, StopScanReturnsIfTaskHandleIsNull)
{
    set_task_handle_null();
    init_node();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_STOP_SCAN, eSetBits)).Times(0);
    scanner->stop_scan();
}

TEST_F(DiscoveryManagerTest, HandleScanProbeNotifiesTask)
{
    DecodedRxPacket decoded = make_decoded_packet(10);

    // Hub must be ready to handle probes
    init_hub();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_SCAN_RESPONSE, eSetBits))
        .WillOnce(Return(pdPASS));
    scanner->handle_scan_probe(decoded);
}

TEST_F(DiscoveryManagerTest, HandleScanProbeReturnsIfNotHub)
{
    init_node();
    DecodedRxPacket decoded = make_decoded_packet(10);

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_SCAN_RESPONSE, eSetBits)).Times(0);
    scanner->handle_scan_probe(decoded);
}

TEST_F(DiscoveryManagerTest, HandleScanProbeReturnsIfTaskHandleIsNull)
{
    set_task_handle_null();
    init_hub();
    DecodedRxPacket decoded = make_decoded_packet(10);

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_SCAN_RESPONSE, eSetBits)).Times(0);
    scanner->handle_scan_probe(decoded);
}

TEST_F(DiscoveryManagerTest, HandleScanResponseNotifiesTask)
{
    DecodedRxPacket decoded = make_decoded_packet(10);

    // Node must be ready to handle scan responses
    init_node();

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_LINK_ALIVE, eSetBits)).WillOnce(Return(pdPASS));
    scanner->handle_scan_response(decoded);
}

TEST_F(DiscoveryManagerTest, HandleScanResponseReturnsIfNotNode)
{
    init_hub();
    DecodedRxPacket decoded = make_decoded_packet(10);

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_LINK_ALIVE, eSetBits)).Times(0);
    scanner->handle_scan_response(decoded);
}

TEST_F(DiscoveryManagerTest, HandleScanResponseReturnsIfTaskHandleIsNull)
{
    set_task_handle_null();
    init_node();
    DecodedRxPacket decoded = make_decoded_packet(10);

    EXPECT_CALL(freertos_hal, task_notify(fake_discovery_task, NOTIFY_LINK_ALIVE, eSetBits)).Times(0);
    scanner->handle_scan_response(decoded);
}

// =============================================================================
// Probes & Responses (Message construction)
// =============================================================================

TEST_F(DiscoveryManagerTest, SendScanProbeHeaderContent)
{
    init_node();
    MessageHeader captured;
    EXPECT_CALL(codec, encode(_, _, _, _, _)).WillOnce(DoAll(SaveArg<0>(&captured), Return(10)));
    EXPECT_CALL(espnow_hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(ESP_OK, scanner->send_scan_probe());
    EXPECT_EQ(captured.msg_type, MessageType::CHANNEL_SCAN_PROBE);
    EXPECT_EQ(captured.sender_node_id, MY_ID);
    EXPECT_EQ(captured.sender_type, MY_TYPE);
    EXPECT_EQ(captured.dest_node_id, ReservedIds::HUB);
}

TEST_F(DiscoveryManagerTest, SendScanResponseHeaderContent)
{
    init_hub();
    // Simulating a probe arrival
    scanner->handle_scan_probe(make_decoded_packet(42));

    MessageHeader captured;
    EXPECT_CALL(codec, encode(_, _, _, _, _)).WillOnce(DoAll(SaveArg<0>(&captured), Return(15)));
    EXPECT_CALL(espnow_hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(ESP_OK, scanner->send_scan_response());
    EXPECT_EQ(captured.msg_type, MessageType::CHANNEL_SCAN_RESPONSE);
    EXPECT_EQ(captured.sender_node_id, MY_ID);
    EXPECT_EQ(captured.dest_node_id, 42);
}

TEST_F(DiscoveryManagerTest, SendScanProbeFailsIfEncodeReturnsZeroLength)
{
    init_node();
    EXPECT_CALL(codec, encode(_, _, _, _, _)).WillOnce(Return(0));
    EXPECT_EQ(ESP_FAIL, scanner->send_scan_probe());
}

TEST_F(DiscoveryManagerTest, SendScanResponseFailsIfEncodeReturnsZeroLength)
{
    init_hub();
    scanner->handle_scan_probe(make_decoded_packet(42));
    EXPECT_CALL(codec, encode(_, _, _, _, _)).WillOnce(Return(0));
    EXPECT_EQ(ESP_FAIL, scanner->send_scan_response());
}

TEST_F(DiscoveryManagerTest, SetChannelBoundaries)
{
    init_node();

    scanner->set_channel(1);
    EXPECT_EQ(1, scanner->get_channel());

    scanner->set_channel(13);
    EXPECT_EQ(13, scanner->get_channel());

    scanner->set_channel(0); // Invalid, should stay 13
    EXPECT_EQ(13, scanner->get_channel());

    scanner->set_channel(14); // Invalid, should stay 13
    EXPECT_EQ(13, scanner->get_channel());
}

// =============================================================================
// Internal Scanning Logic (scan_channel)
// =============================================================================
// clearOnExit == NOTIFY_STOP_SCAN  -> is should_stop_scan()
// clearOnExit == NOTIFY_LINK_ALIVE -> is hub_was_found()

TEST_F(DiscoveryManagerTest, ScanChannelFoundOnFirstAttempt)
{
    init_node();
    scanner->set_channel(VALID_CHANNEL);

    ON_CALL(wifi_hal, wifi_set_channel(_, _)).WillByDefault(Return(ESP_OK));
    ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(espnow_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

    // should_stop_scan(): clearOnExit=NOTIFY_STOP_SCAN, timeout=0 → not found
    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_STOP_SCAN, _, 0)).WillOnce(Return(pdFAIL));

    // hub_was_found(): clearOnExit=NOTIFY_LINK_ALIVE, timeout>0 → found
    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_LINK_ALIVE, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_LINK_ALIVE), Return(pdPASS)));

    EXPECT_EQ(ESP_OK, scanner->scan_channel());
    EXPECT_EQ(VALID_CHANNEL, scanner->get_channel());
    EXPECT_FALSE(scanner->is_scanning());
}

TEST_F(DiscoveryManagerTest, ScanChannelStopOnShouldStopScan)
{
    init_node();
    scanner->set_channel(VALID_CHANNEL);

    ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(wifi_hal, wifi_set_channel(_, _)).WillByDefault(Return(ESP_OK));
    ON_CALL(espnow_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

    // should_stop_scan(): NOTIFY_STOP_SCAN received -> stop
    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_STOP_SCAN, _, 0))
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_STOP_SCAN), Return(pdPASS)));

    // hub_was_found() should not be called
    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_LINK_ALIVE, _, _)).Times(0);

    EXPECT_EQ(ESP_FAIL, scanner->scan_channel());
    EXPECT_FALSE(scanner->is_scanning());
}

TEST_F(DiscoveryManagerTest, ScanChannelSequenceOrder)
{
    init_node();
    scanner->set_channel(6);

    ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(espnow_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

    Sequence s;

    uint8_t expected_channels[] = {6, 7, 8, 9, 10, 11, 12, 13, 1, 2, 3, 4, 5};
    for (uint8_t ch : expected_channels) {
        EXPECT_CALL(wifi_hal, wifi_set_channel(ch, _)).InSequence(s).WillOnce(Return(ESP_OK));

        // For each attempt (repeat this block SCAN_CHANNEL_ATTEMPTS times if > 1):
        for (int i = 0; i < SCAN_CHANNEL_ATTEMPTS; i++) {
            // should_stop_scan: clearOnExit=NOTIFY_STOP_SCAN, timeout=0 → not stopping
            EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_STOP_SCAN, _, 0))
                .InSequence(s)
                .WillOnce(Return(pdFAIL));

            // hub_was_found: clearOnExit=NOTIFY_LINK_ALIVE, timeout>0 → not found
            EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_LINK_ALIVE, _, _))
                .InSequence(s)
                .WillOnce(Return(pdFAIL));
        }
    }

    EXPECT_EQ(ESP_FAIL, scanner->scan_channel());
}

TEST_F(DiscoveryManagerTest, ScanChannelContinuesToNextChannelIfSetChannelFails)
{
    init_node();
    scanner->set_channel(VALID_CHANNEL);

    ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(espnow_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

    Sequence s;

    // First channel fails
    EXPECT_CALL(wifi_hal, wifi_set_channel(VALID_CHANNEL, _)).InSequence(s).WillOnce(Return(ESP_FAIL));

    // Next channel succeeds
    EXPECT_CALL(wifi_hal, wifi_set_channel(VALID_CHANNEL + 1, _)).InSequence(s).WillOnce(Return(ESP_OK));

    // should_stop_scan: clearOnExit=NOTIFY_STOP_SCAN, timeout=0 → not stopping
    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_STOP_SCAN, _, 0)).InSequence(s).WillOnce(Return(pdFAIL));

    // hub_was_found: clearOnExit=NOTIFY_LINK_ALIVE, timeout>0 → not found
    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_LINK_ALIVE, _, _))
        .InSequence(s)
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_LINK_ALIVE), Return(pdPASS)));

    EXPECT_EQ(ESP_OK, scanner->scan_channel());
    EXPECT_EQ(VALID_CHANNEL + 1, scanner->get_channel());
}

TEST_F(DiscoveryManagerTest, ScanChannelGoesToNextChannelIfAllAttemptsFail)
{
    init_node();
    scanner->set_channel(VALID_CHANNEL);
    ON_CALL(wifi_hal, wifi_set_channel(_, _)).WillByDefault(Return(ESP_OK));
    ON_CALL(codec, encode(_, _, _, _, _)).WillByDefault(Return(10));

    Sequence s;

    // All attempts fails
    for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS; ++attempt) {
        EXPECT_CALL(espnow_hal, hal_esp_now_send(_, _, _)).InSequence(s).WillOnce(Return(ESP_FAIL));
        // send_scan_probe() failed → should_stop_scan and hub_was_found are NOT called
    }

    // Next attempt succeeds
    EXPECT_CALL(espnow_hal, hal_esp_now_send(_, _, _)).InSequence(s).WillOnce(Return(ESP_OK));

    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_STOP_SCAN, _, 0)).InSequence(s).WillOnce(Return(pdFAIL));

    EXPECT_CALL(freertos_hal, task_notify_wait(0, NOTIFY_LINK_ALIVE, _, _))
        .InSequence(s)
        .WillOnce(DoAll(SetArgPointee<2>(NOTIFY_LINK_ALIVE), Return(pdPASS)));

    EXPECT_EQ(ESP_OK, scanner->scan_channel());
    EXPECT_EQ(VALID_CHANNEL + 1, scanner->get_channel()); // found on channel VALID_CHANNEL + 1
    EXPECT_FALSE(scanner->is_scanning());
}

TEST_F(DiscoveryManagerTest, DefaultChannelPolicyIsScan)
{
    init_node();
    EXPECT_EQ(ChannelPolicy::SCAN, scanner->get_channel_policy());
}

TEST_F(DiscoveryManagerTest, SetAndGetChannelPolicy)
{
    init_node();
    scanner->set_channel_policy(ChannelPolicy::FIXED);
    EXPECT_EQ(ChannelPolicy::FIXED, scanner->get_channel_policy());

    scanner->set_channel_policy(ChannelPolicy::SCAN);
    EXPECT_EQ(ChannelPolicy::SCAN, scanner->get_channel_policy());
}

TEST_F(DiscoveryManagerTest, ScanChannelSkipsChannelSwitchingWhenPolicyFixed)
{
    init_node();
    scanner->set_channel_policy(ChannelPolicy::FIXED);

    // wifi_set_channel must NEVER be called when policy is FIXED
    EXPECT_CALL(wifi_hal, wifi_set_channel(_, _)).Times(0);
    EXPECT_CALL(espnow_hal, hal_esp_now_send(_, _, _)).Times(0);

    EXPECT_EQ(ESP_OK, scanner->scan_channel());
    EXPECT_FALSE(scanner->is_scanning());
}