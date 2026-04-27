// include/interfaces/i_hal_wifi.hpp
#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
namespace espnow {

/**
 * @interface IWiFiHAL
 * @brief Hardware Abstraction Layer for WiFi driver.
 * @internal
 */
class IWiFiHAL
{
public:
    virtual ~IWiFiHAL() = default;

    /** @copydoc esp_wifi_get_mode() */
    virtual esp_err_t wifi_get_mode(wifi_mode_t *mode) = 0;

    /** @copydoc esp_wifi_set_channel() */
    virtual esp_err_t wifi_set_channel(uint8_t primary, wifi_second_chan_t second) = 0;

    /** @copydoc esp_wifi_get_channel() */
    virtual esp_err_t wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second) = 0;
};

} // namespace espnow
