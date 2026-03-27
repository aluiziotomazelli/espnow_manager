// include/interfaces/i_channel_observer.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

/**
 * @interface IChannelMonitor
 * @brief Observer interface for WiFi channel discovery events.
 */
class IChannelMonitor
{
public:
    virtual ~IChannelMonitor() = default;

    /**
     * @brief Initializes the channel monitor.
     * @param interval_ms The interval in milliseconds to check the WiFi channel.
     * @param rx_task_handle The RX task handle.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init(uint32_t interval_ms, TaskHandle_t rx_task_handle) = 0;

    /**
     * @brief Gets the current WiFi channel.
     * @return The current WiFi channel.
     */
    virtual uint8_t get_wifi_channel() = 0;

    /**
     * @brief Ticks the channel monitor.
     * @param now_ms The current time in milliseconds.
     */
    virtual void tick(uint64_t now_ms) = 0;
};
