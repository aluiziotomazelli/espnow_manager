// host_test/common/mock_wifi_driver_hal.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_hal_wifi.hpp"

class MockStorageManager : public IStorageManager
{
public:
    MOCK_METHOD(esp_err_t, load, (uint8_t &, std::vector<PersistentPeer> &), (override));
    MOCK_METHOD(esp_err_t, save, (uint8_t, const std::vector<PersistentPeer> &, bool), (override));
};
