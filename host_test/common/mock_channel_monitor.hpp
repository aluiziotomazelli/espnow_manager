// host_test/common/mock_channel_monitor.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_channel_monitor.hpp"

namespace espnow {

class MockChannelMonitor : public IChannelMonitor
{
public:
    MOCK_METHOD(esp_err_t, init, (uint32_t interval_ms, TaskHandle_t rx_task_handle), (override));
    MOCK_METHOD(void, deinit, (), (override));
    MOCK_METHOD(void, tick, (int64_t now_ms), (override));
    MOCK_METHOD(uint8_t, get_wifi_channel, (), (override));
};

} // namespace espnow
