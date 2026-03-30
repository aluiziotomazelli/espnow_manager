// include/interfaces/i_channel_monitor.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * @interface IChannelMonitor
 * @brief Monitors WiFi channel and notifies on channel changes.
 * @internal
 */
class IChannelMonitor
{
public:
    virtual ~IChannelMonitor() = default;

    /**
     * @brief Initializes the channel monitor.
     * @param interval_ms Interval in milliseconds to check the WiFi channel.
     * @param rx_task_handle RX task handle for notifications.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init(uint32_t interval_ms, TaskHandle_t rx_task_handle) = 0;

    /**
     * @brief Deinitializes the channel monitor.
     */
    virtual void deinit() = 0;

    /**
     * @brief Gets the current WiFi channel.
     * @return Current WiFi channel.
     */
    virtual uint8_t get_wifi_channel() = 0;

    /**
     * @brief Ticks the channel monitor.
     * @param now_ms Current time in milliseconds.
     */
    virtual void tick(uint64_t now_ms) = 0;
};
