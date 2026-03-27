// src/channel_monitor.cpp
#include "esp_log.h"

#include "channel_monitor.hpp"

static const char* TAG = "ChannelMonitor";

ChannelMonitor::ChannelMonitor(IWiFiHAL& hal_wifi, IFreeRTOSHAL& hal_freertos)
    : hal_wifi_(hal_wifi)
    , hal_freertos_(hal_freertos)
{
}

ChannelMonitor::~ChannelMonitor()
{
    is_active_ = false;
}

esp_err_t ChannelMonitor::init(uint32_t interval_ms, TaskHandle_t rx_task_handle)
{
    if (rx_task_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    rx_task_handle_ = rx_task_handle;
    interval_ms_ = interval_ms;
    last_known_channel_ = verify_wifi_channel();
    is_active_ = true;
    return ESP_OK;
}

void ChannelMonitor::tick(uint64_t now_ms)
{
    // If not initialized or the interval has not passed, nothing to do here
    if (!is_active_ || (now_ms - last_check_ms_ < interval_ms_)) {
        return;
    }

    // When the interval has passed, update the last check time
    last_check_ms_ = now_ms;

    uint8_t current_channel = verify_wifi_channel(); // Get the current WiFi channel

    if (current_channel != last_known_channel_.load()) { // Check if the channel has changed
        last_known_channel_.store(current_channel);      // Update the last known channel member
        notify_rx_task(NOTIFY_CHANNEL_CHANGED);          // Notify rx_task about channel change
    }
}

uint8_t ChannelMonitor::verify_wifi_channel()
{
    uint8_t channel;
    esp_err_t err = hal_wifi_.wifi_get_channel(&channel, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi channel: %s", esp_err_to_name(err));
        return last_known_channel_.load();
    }
    return channel;
}

void ChannelMonitor::notify_rx_task(uint32_t notifications)
{
    hal_freertos_.task_notify(rx_task_handle_, notifications, eSetBits);
}