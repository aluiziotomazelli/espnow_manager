// host_test/common/mock_wifi_driver_hal.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_en_hal_timer.hpp"

namespace espnow {

class MockTimerHAL : public ITimerHAL
{
public:
    MOCK_METHOD(int64_t, get_time_us, (), (const override));
};

} // namespace espnow
