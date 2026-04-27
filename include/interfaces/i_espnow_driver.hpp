// include/interfaces/i_espnow_driver.hpp
#pragma once

#include "esp_err.h"
#include "esp_now.h"

#include "espnow_types.hpp" // EspNowConfig
namespace espnow {

/**
 * @interface IEspNowDriver
 * @brief ESP-NOW driver interface (internal)
 * @internal
 */
class IEspNowDriver
{
public:
    virtual ~IEspNowDriver() = default;

    virtual esp_err_t init(const EspNowConfig &config, esp_now_recv_cb_t recv_cb, esp_now_send_cb_t send_cb) = 0;

    virtual esp_err_t deinit() = 0;
};
} // namespace espnow
