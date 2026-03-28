#pragma once

#include <cstdint>

/**
 * @file i_timer_hal.hpp
 * @brief Interface for system time services.
 */

/**
 * @interface ITimerHAL
 * @brief Interface for system time services.
 * @internal
 */
class ITimerHAL
{
public:
    virtual ~ITimerHAL() = default;

    /** @copydoc esp_timer_get_time() */
    virtual uint64_t get_time_us() const = 0;
};