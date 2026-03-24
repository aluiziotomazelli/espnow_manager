// include/interfaces/i_channel_observer.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"

#include "i_channel_observer.hpp"

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
     * @param observer The channel observer.
     * @param interval_ms The interval in milliseconds to check the WiFi channel.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init(IChannelObserver *observer, uint32_t interval_ms) = 0;

    /**
     * @brief Ticks the channel monitor.
     * @param now_ms The current time in milliseconds.
     */
    virtual void tick(uint64_t now_ms) = 0;
};
