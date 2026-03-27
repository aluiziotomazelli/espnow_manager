// host_test/test_discovery_manager/main/test_discovery_manager.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "hal_real_freertos.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_message_codec.hpp"

#include "discovery_manager.hpp"

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
    using DiscoveryManager::get_task_handle;
    using DiscoveryManager::hub_was_found;
    using DiscoveryManager::make_probe_header;
    using DiscoveryManager::make_response_header;
    using DiscoveryManager::scan_channel;
    using DiscoveryManager::send_scan_probe;
    using DiscoveryManager::send_scan_response;
    using DiscoveryManager::should_stop_scan;
};

// =============================================================================
// Fixture
// =============================================================================

class DiscoveryManagerTaskTest : public ::testing::Test
{
protected:
    // Owned pointers to correct destruction and no mock leakage on tests
    std::unique_ptr<NiceMock<MockWiFiHAL>> hal_owned;
    std::unique_ptr<NiceMock<MockMessageCodec>> codec_owned;

    // Raw pointers to use in tests
    NiceMock<MockWiFiHAL>* wifi_hal;
    NiceMock<MockMessageCodec>* codec;

    RealFreeRTOSHAL freertos_hal;
    std::unique_ptr<TestableDiscoveryManager> scanner;

    static constexpr NodeId MY_ID = 2;
    static constexpr NodeType MY_TYPE = 0x02;
    static constexpr uint8_t VALID_CHANNEL = 6;
    static constexpr uint32_t stack_size = 2048;
    static constexpr UBaseType_t priority = 5;

    static constexpr uint16_t delay_10_ms = 10;
    static constexpr uint16_t delay_20_ms = 20;
    static constexpr uint16_t delay_50_ms = 50;
    static constexpr uint16_t delay_100_ms = 100;

    // Real RX task to test task notification
    bool notify_channel_found = false;
    bool notify_scan_failed = false;

    TaskHandle_t real_rx_task_handle = nullptr;
    void make_real_rx_task()
    {
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<DiscoveryManagerTaskTest*>(arg);
                uint32_t notifications = 0;
                while (true) {
                    if (xTaskNotifyWait(0, NOTIFY_ALL, &notifications, portMAX_DELAY)) {
                        if (notifications & NOTIFY_CHANNEL_FOUND) {
                            self->notify_channel_found = true;
                        }
                        if (notifications & NOTIFY_SCAN_FAILED) {
                            self->notify_scan_failed = true;
                        }
                    }
                }
            },
            "RxTask",
            2048,
            this,
            5,
            &real_rx_task_handle);
    }

    void SetUp() override
    {
        hal_owned = std::make_unique<NiceMock<MockWiFiHAL>>();
        codec_owned = std::make_unique<NiceMock<MockMessageCodec>>();

        wifi_hal = hal_owned.get();
        codec = codec_owned.get();

        scanner = std::make_unique<TestableDiscoveryManager>(*wifi_hal, *codec, freertos_hal);
    }

    void TearDown() override
    {
        scanner->deinit(); // stops the discovery_task first
        vTaskDelay(pdMS_TO_TICKS(delay_50_ms));

        if (real_rx_task_handle) {
            vTaskDelete(real_rx_task_handle);
            real_rx_task_handle = nullptr;
        }

        scanner.reset();
    }

    // Helper: init and give task time to start and block
    void init_node_and_wait()
    {
        scanner->init(MY_ID, MY_TYPE, real_rx_task_handle, priority, stack_size);
        vTaskDelay(pdMS_TO_TICKS(delay_10_ms));
    }
    void init_hub_and_wait()
    {
        scanner->init(MY_ID, ReservedTypes::HUB, real_rx_task_handle, priority, stack_size);
        vTaskDelay(pdMS_TO_TICKS(delay_10_ms));
    }

    void notify_discovery_task(uint32_t notification)
    {
        xTaskNotify(scanner->get_task_handle(), notification, eSetBits);
    }

    DecodedPacket make_decoded_packet(NodeId sender_id)
    {
        DecodedPacket decoded{};
        decoded.header.sender_node_id = sender_id;
        return decoded;
    }
};

// =============================================================================
// Tests
// =============================================================================

TEST_F(DiscoveryManagerTaskTest, SmokingTest)
{
    EXPECT_TRUE(true);
}

TEST_F(DiscoveryManagerTaskTest, ScanChannelSuccessNotifyChannelFoundOnRxTask)
{
    make_real_rx_task();
    init_node_and_wait();
    scanner->set_channel(VALID_CHANNEL);

    ON_CALL(*wifi_hal, wifi_set_channel(_)).WillByDefault(Return(ESP_OK));
    ON_CALL(*codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(*wifi_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

    // Trigger the scan via the task notification (not direct call)
    scanner->start_scan();
    vTaskDelay(pdMS_TO_TICKS(delay_10_ms)); // give task time to send probe and block in hub_was_found()

    // Simulate hub responding: inject NOTIFY_LINK_ALIVE into the discovery task
    notify_discovery_task(NOTIFY_LINK_ALIVE);
    vTaskDelay(pdMS_TO_TICKS(delay_10_ms)); // give task time to finish scan and notify rx_task

    EXPECT_TRUE(notify_channel_found);
}

TEST_F(DiscoveryManagerTaskTest, ScanChannelFailureNotifiesScanFailedOnRxTask)
{
    make_real_rx_task();
    init_node_and_wait();
    scanner->set_channel(VALID_CHANNEL);

    // Probe always fails → scan_channel returns ESP_FAIL → task sends NOTIFY_SCAN_FAILED
    ON_CALL(*wifi_hal, wifi_set_channel(_)).WillByDefault(Return(ESP_OK));
    ON_CALL(*codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(*wifi_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_FAIL));

    scanner->start_scan();

    // No NOTIFY_LINK_ALIVE injected — scan exhausts all channels and fails
    // Worst case: 13 channels × SCAN_CHANNEL_ATTEMPTS × SCAN_CHANNEL_TIMEOUT_MS
    const uint32_t scan_timeout_ms = SCAN_CHANNEL_TIMEOUT_MS * SCAN_CHANNEL_ATTEMPTS * 13 + 100;
    vTaskDelay(pdMS_TO_TICKS(scan_timeout_ms));

    EXPECT_TRUE(notify_scan_failed);
}

TEST_F(DiscoveryManagerTaskTest, StopScanAbortsScanAndNotifiesScanFailed)
{
    make_real_rx_task();
    init_node_and_wait();
    scanner->set_channel(VALID_CHANNEL);

    ON_CALL(*wifi_hal, wifi_set_channel(_)).WillByDefault(Return(ESP_OK));
    ON_CALL(*codec, encode(_, _, _, _, _)).WillByDefault(Return(10));
    ON_CALL(*wifi_hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));

    scanner->start_scan();
    vTaskDelay(pdMS_TO_TICKS(delay_10_ms)); // task is now blocked in hub_was_found()

    scanner->stop_scan(); // injects NOTIFY_STOP_SCAN into discovery task
    vTaskDelay(pdMS_TO_TICKS(delay_10_ms));

    EXPECT_TRUE(notify_scan_failed);
}

TEST_F(DiscoveryManagerTaskTest, HubSendsScanResponseOnProbeReceived)
{
    make_real_rx_task();
    init_hub_and_wait();

    MessageHeader captured;
    ON_CALL(*codec, encode(_, _, _, _, _)).WillByDefault(DoAll(SaveArg<0>(&captured), Return(10)));
    EXPECT_CALL(*wifi_hal, hal_esp_now_send(_, _, _)).Times(1).WillOnce(Return(ESP_OK));

    scanner->handle_scan_probe(make_decoded_packet(42));
    vTaskDelay(pdMS_TO_TICKS(delay_10_ms));

    EXPECT_EQ(captured.msg_type, MessageType::CHANNEL_SCAN_RESPONSE);
    EXPECT_EQ(captured.dest_node_id, 42);
}