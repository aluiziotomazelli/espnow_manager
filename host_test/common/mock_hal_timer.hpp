// host_test/common/mock_wifi_driver_hal.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_hal_timer.hpp"

class MockTimerHAL : public ITimerHAL
{
public:
    MOCK_METHOD(uint64_t, get_time_us, (), (const override));
};
