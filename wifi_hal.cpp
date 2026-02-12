#include "wifi_hal.hpp"
#include "esp_wifi.h"
#include "esp_now.h"

RealWiFiHAL::RealWiFiHAL()
    : task_handle_(nullptr)
{
}

esp_err_t RealWiFiHAL::set_channel(uint8_t channel)
{
    return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
}

esp_err_t RealWiFiHAL::get_channel(uint8_t *channel)
{
    return esp_wifi_get_channel(channel, nullptr);
}

esp_err_t RealWiFiHAL::send_packet(const uint8_t *mac, const uint8_t *data, size_t len)
{
    return esp_now_send(mac, data, len);
}

bool RealWiFiHAL::wait_for_event(uint32_t event_mask, uint32_t timeout_ms)
{
    uint32_t notifications = 0;
    if (xTaskNotifyWait(0, event_mask, &notifications, pdMS_TO_TICKS(timeout_ms)) == pdPASS)
    {
        return (notifications & event_mask) != 0;
    }
    return false;
}
