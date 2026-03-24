#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "channel_monitor.hpp"
#include "mock_hal_wifi.hpp"
#include "i_channel_observer.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class MockChannelObserver : public IChannelObserver
{
public:
    MOCK_METHOD(void, on_channel_found_cb, (uint8_t channel), (override));
    MOCK_METHOD(void, on_scan_failed_cb, (), (override));
    MOCK_METHOD(void, on_scan_started_cb, (), (override));
    MOCK_METHOD(void, on_channel_changed_cb, (uint8_t new_channel), (override));
};

class ChannelMonitorTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> wifi_hal;
    NiceMock<MockChannelObserver> observer;
    std::unique_ptr<ChannelMonitor> monitor;

    static constexpr uint32_t INTERVAL_MS = 5000;
    static constexpr uint8_t INITIAL_CHANNEL = 1;
    static constexpr wifi_second_chan_t INITIAL_SECOND_CHANNEL = WIFI_SECOND_CHAN_NONE;

    void SetUp() override
    {
        monitor = std::make_unique<ChannelMonitor>(wifi_hal);

        // Mock default behavior for wifi_get_channel
        ON_CALL(wifi_hal, wifi_get_channel(_, _))
            .WillByDefault(Invoke([](uint8_t *channel, wifi_second_chan_t *second) {
                *channel = INITIAL_CHANNEL;
                return ESP_OK;
            }));
    }
    void init_monitor() { monitor->init(&observer, INTERVAL_MS); }
};

// =========================================================================
// Basic Tests (Stubs for the user to complete)
// =========================================================================

TEST_F(ChannelMonitorTest, InitSucceeds)
{
    init_monitor();
    SUCCEED();
}

TEST_F(ChannelMonitorTest, InitFailsWhenObserverIsNull)
{
    esp_err_t err = monitor->init(nullptr, INTERVAL_MS);
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
    monitor->init(nullptr, INTERVAL_MS);                    // Init with nullptr will not set is_active_ to true
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _)).Times(0); // No tick will be performed
    monitor->tick(INTERVAL_MS);
}

TEST_F(ChannelMonitorTest, NotifyObserverWhenChannelChanges)
{
    init_monitor();
    uint8_t new_channel = 6;

    // First tick to establish current channel
    monitor->tick(0);

    // Mock wifi change
    EXPECT_CALL(wifi_hal, wifi_get_channel(_, _))
        .WillOnce(Invoke([new_channel](uint8_t *ch, wifi_second_chan_t *second) {
            *ch = new_channel;
            return ESP_OK;
        }));

    // Expect observer notification
    EXPECT_CALL(observer, on_channel_changed_cb(new_channel)).Times(1);

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
        .WillOnce(Invoke([new_channel](uint8_t *ch, wifi_second_chan_t *second) {
            *ch = new_channel;
            return ESP_FAIL;
        }));

    // Expect observer notification not to be called
    EXPECT_CALL(observer, on_channel_changed_cb(new_channel)).Times(0);

    monitor->tick(INTERVAL_MS);
}