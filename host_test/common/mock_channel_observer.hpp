// host_test/common/mock_channel_observer.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_channel_observer.hpp"

class MockChannelObserver : public IChannelObserver
{
public:
    MOCK_METHOD(void, on_channel_found_cb, (uint8_t channel), (override));
    MOCK_METHOD(void, on_scan_failed_cb, (), (override));
    MOCK_METHOD(void, on_scan_started_cb, (), (override));
    MOCK_METHOD(void, on_channel_changed_cb, (uint8_t new_channel), (override));
};