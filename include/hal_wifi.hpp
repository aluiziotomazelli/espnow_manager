// include/hal_wifi.hpp
#pragma once

#include "esp_wifi.h"
#include "i_en_hal_wifi.hpp"
namespace espnow {

/**
 * @brief Hardware Abstraction Layer implementation for WiFi driver.
 * @internal
 */
class WiFiHAL : public IWiFiHAL
{
public:
    WiFiHAL() = default;

    /** @copydoc IWiFiHAL::wifi_get_mode() */
    esp_err_t wifi_get_mode(wifi_mode_t* mode) override { return esp_wifi_get_mode(mode); }

    /** @copydoc IWiFiHAL::wifi_set_channel() */
    esp_err_t wifi_set_channel(uint8_t primary, wifi_second_chan_t second) override
    {
        return esp_wifi_set_channel(primary, second);
    }

    /** @copydoc IWiFiHAL::wifi_get_channel() */
    esp_err_t wifi_get_channel(uint8_t* primary, wifi_second_chan_t* second) override
    {
        return esp_wifi_get_channel(primary, second);
    }
};

} // namespace espnow
