#pragma once

#include "esp_timer.h"
#include "interfaces/i_timer_hal.hpp"

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
    ~TimerHAL() override = default;

    /**
     * @copydoc ITimerHAL::get_time_ms()
     */
    uint64_t get_time_ms() const override { return esp_timer_get_time() / 1000; }
};
