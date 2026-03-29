// include/espnow_driver.hpp
#pragma once

#include "esp_err.h"

#include "i_espnow_driver.hpp"
#include "i_hal_espnow.hpp"
#include "i_hal_wifi.hpp"
// #include "i_hal_freertos.hpp"
// #include "i_peer_manager.hpp"
// #include "i_tx_manager.hpp"
// #include "i_heartbeat_manager.hpp"

/**
 * @class EspNowDriver
 * @brief ESP-NOW driver class.
 * @internal
 */
class EspNowDriver : public IEspNowDriver
{
public:
    EspNowDriver(IWiFiHAL &wifi_hal, IEspNowHAL &espnow_hal);

    /** @copydoc IEspNowDriver::init */
    esp_err_t init(const EspNowConfig &config, esp_now_recv_cb_t recv_cb, esp_now_send_cb_t send_cb) override;

    /** @copydoc IEspNowDriver::deinit */
    esp_err_t deinit() override;

private:
    IWiFiHAL &wifi_hal_;
    IEspNowHAL &espnow_hal_;

    esp_err_t add_broadcast_peer();
    esp_err_t init_fail(esp_err_t ret, const char *step);
};