#pragma once

#include <cstdint>

/**
 * @file i_timer_hal.hpp
 * @brief Interface for system time services.
 */

/**
 * @class ITimerHAL
 * @brief Interface for system time services.
 * @internal
 */
class ITimerHAL
{
public:
    virtual ~ITimerHAL() = default;

    /**
     * @brief Get system time in milliseconds.
     * @internal
     * @return uint64_t uptime in ms.
     */
    virtual uint64_t get_time_us() const = 0;
};