#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_en_hal_wifi.hpp"
#include "mock_en_hal_freertos.hpp"
#include "protocol_types.hpp"

#include "channel_monitor.hpp"
using namespace espnow;

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class ChannelMonitorTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> wifi_hal;
    NiceMock<MockFreeRTOSHAL> freertos_hal;
    std::unique_ptr<ChannelMonitor> monitor;

    static constexpr uint32_t INTERVAL_MS = 5000;
    static constexpr uint8_t INITIAL_CHANNEL = 1;
    static constexpr wifi_second_chan_t INITIAL_SECOND_CHANNEL = WIFI_SECOND_CHAN_NONE;
    TaskHandle_t fake_rx_task_handle_ = reinterpret_cast<TaskHandle_t>(0x6);

    void SetUp() override
    {
        monitor = std::make_unique<ChannelMonitor>(wifi_hal, freertos_hal);

        // Mock default behavior for wifi_get_channel
        ON_CALL(wifi_hal, wifi_get_channel(_, _))
            .WillByDefault(Invoke([](uint8_t* channel, wifi_second_chan_t* second) {
                *channel = INITIAL_CHANNEL;
                return ESP_OK;
            }));
    }
    void init_monitor() { monitor->init(INTERVAL_MS, fake_rx_task_handle_); }
};

// =========================================================================
// Basic Tests (Stubs for the user to complete)
// =========================================================================

TEST_F(ChannelMonitorTest, InitSucceeds)
{
    init_monitor();
    SUCCEED();
}

TEST_F(ChannelMonitorTest, InitFailsWhenRxTaskHandleIsNull)
{
    esp_err_t err = monitor->init(INTERVAL_MS, nullptr);
    EXPECT_NE(err, ESP_OK);
}

TEST_F(ChannelMonitorTest, TickBeforeIntervalDoesNotCheckWifi)
{
    init_monitor();
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _)).Times(0);
    monitor->tick(INTERVAL_MS - 1);
}

TEST_F(ChannelMonitorTest, TickAtIntervalChecksWifi)
{
    init_monitor();
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _)).Times(1);
    monitor->tick(INTERVAL_MS);
}

TEST_F(ChannelMonitorTest, TickUnitializedDoesNothing)
{
    monitor->init(INTERVAL_MS, nullptr);                    // Init with nullptr will not set is_active_ to true
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _)).Times(0); // No tick will be performed
    monitor->tick(INTERVAL_MS);
}

TEST_F(ChannelMonitorTest, NotifyRxTaskWhenChannelChanges)
{
    init_monitor();
    uint8_t new_channel = 6;

    // First tick to establish current channel
    monitor->tick(0);

    // Mock wifi change
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _))
        .WillOnce(Invoke([new_channel](uint8_t* ch, wifi_second_chan_t* second) {
            *ch = new_channel;
            return ESP_OK;
        }));

    // Expect rx_task notification
    EXPECT_CALL(freertos_hal, task_notify(fake_rx_task_handle_, NOTIFY_CHANNEL_CHANGED, eSetBits))
        .Times(1)
        .WillOnce(Return(pdPASS));

    monitor->tick(INTERVAL_MS);
}

TEST_F(ChannelMonitorTest, MonitorReturnTheSAmeChannelIfWifiGetChannelFail)
{
    init_monitor();
    uint8_t new_channel = 6;

    // First tick to establish current channel
    monitor->tick(0);

    // Mock wifi change with different channel but failing to get it
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _))
        .WillOnce(Invoke([new_channel](uint8_t* ch, wifi_second_chan_t* second) {
            *ch = new_channel;
            return ESP_FAIL;
        }));

    // Expect rx_task notification not to be called
    EXPECT_CALL(freertos_hal, task_notify(fake_rx_task_handle_, NOTIFY_CHANNEL_CHANGED, eSetBits)).Times(0);

    monitor->tick(INTERVAL_MS);
}

TEST_F(ChannelMonitorTest, DeinitDisablesMonitoring)
{
    init_monitor();
    monitor->tick(0);

    // Deinit should disable monitoring
    monitor->deinit();

    // After deinit, tick() should not call wifi_get_channel because is_active_ is false
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _)).Times(0);
    EXPECT_CALL(freertos_hal, task_notify(_, _, _)).Times(0);

    monitor->tick(INTERVAL_MS);
}