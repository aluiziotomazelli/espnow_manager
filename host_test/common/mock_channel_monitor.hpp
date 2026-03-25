// host_test/common/mock_channel_monitor.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_channel_monitor.hpp"

class MockChannelMonitor : public IChannelMonitor
{
public:
    MOCK_METHOD(esp_err_t, init, (IChannelObserver *observer, uint32_t interval_ms), (override));
    MOCK_METHOD(void, tick, (uint64_t now_ms), (override));
};
