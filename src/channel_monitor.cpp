// src/channel_monitor.cpp
#include "esp_log.h"

#include "channel_monitor.hpp"

static const char *TAG = "ChannelMonitor";

ChannelMonitor::ChannelMonitor(IWiFiHAL &hal_wifi)
    : hal_wifi_(hal_wifi)
{
}

ChannelMonitor::~ChannelMonitor()
{
    is_active_ = false;
}

esp_err_t ChannelMonitor::init(IChannelObserver *observer, uint32_t interval_ms)
{
    if (observer == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    observer_ = observer;
    interval_ms_ = interval_ms;
    last_known_channel_ = get_wifi_channel();
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

    uint8_t current_channel = get_wifi_channel(); // Get the current WiFi channel

    if (current_channel != last_known_channel_) {    // Check if the channel has changed
        last_known_channel_ = current_channel;       // Update the last known channel member
        notify_channel_changed(last_known_channel_); // Notify the observer
    }
}

uint8_t ChannelMonitor::get_wifi_channel()
{
    uint8_t channel;
    esp_err_t err = hal_wifi_.wifi_get_channel(&channel, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get WiFi channel: %s", esp_err_to_name(err));
        return last_known_channel_;
    }
    return channel;
}

void ChannelMonitor::notify_channel_changed(uint8_t new_channel)
{
    ESP_LOGI(TAG, "WiFi channel changed from %d to %d", last_known_channel_, new_channel);
    observer_->on_channel_changed_cb(new_channel);
}