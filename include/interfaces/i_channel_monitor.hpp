// include/interfaces/i_channel_observer.hpp
#pragma once

#include <cstdint>

/**
 * @interface IChannelMonitor
 * @brief Observer interface for WiFi channel discovery events.
 */
class IChannelMonitor
{
public:
    virtual ~IChannelMonitor() = default;

    /**
     * @brief Ticks the channel monitor.
     * @param now_ms The current time in milliseconds.
     */
    virtual void tick(uint64_t now_ms) = 0;

    virtual uint8_t get_wifi_channel() const = 0;
};
