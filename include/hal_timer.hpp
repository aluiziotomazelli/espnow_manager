#pragma once

#include "esp_timer.h"
#include "i_hal_timer.hpp"

/**
 * @file esp_timer_hal.hpp
 * @brief ESP-IDF implementation of system time services.
 */

/**
 * @class TimerHAL
 * @brief ESP-IDF implementation of ITimerHAL using esp_timer.
 */
class TimerHAL : public ITimerHAL
{
public:
    TimerHAL() = default;

    /**
     * @copydoc ITimerHAL::get_time_ms()
     */
    uint64_t get_time_us() const override { return esp_timer_get_time(); }
};
