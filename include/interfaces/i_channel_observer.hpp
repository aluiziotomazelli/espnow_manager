// include/interfaces/i_channel_observer.hpp
#pragma once

#include <cstdint>

/**
 * @interface IChannelObserver
 * @brief Observer interface for WiFi channel discovery events.
 */
class IChannelObserver
{
public:
    virtual ~IChannelObserver() = default;

    /**
     * @brief Called when a Hub is discovered on a specific channel.
     * @param channel The WiFi channel where the Hub was found.
     */
    virtual void on_channel_found_cb(uint8_t channel) = 0;
};
