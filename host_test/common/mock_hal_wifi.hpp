// host_test/common/mock_hal_wifi.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_hal_wifi.hpp"

class MockWiFiHAL : public IWiFiHAL
{
public:
    MOCK_METHOD(esp_err_t, wifi_get_mode, (wifi_mode_t *mode), (override));
    MOCK_METHOD(esp_err_t, wifi_set_channel, (uint8_t primary, wifi_second_chan_t second), (override));
    MOCK_METHOD(esp_err_t, wifi_get_channel, (uint8_t *primary, wifi_second_chan_t *second), (override));
};
