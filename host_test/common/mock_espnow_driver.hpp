// host_test/common/mock_espnow_driver.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_espnow_driver.hpp"

class MockEspNowDriver : public IEspNowDriver
{
public:
    MOCK_METHOD(
        esp_err_t,
        init,
        (const EspNowConfig &config, esp_now_recv_cb_t recv_cb, esp_now_send_cb_t send_cb),
        (override));

    MOCK_METHOD(esp_err_t, deinit, (), (override));
};
