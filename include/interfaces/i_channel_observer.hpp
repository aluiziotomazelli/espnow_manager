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
     * @brief Called when the WiFi channel has changed.
     */
    virtual void on_channel_changed_cb(uint8_t new_channel) = 0;
};
