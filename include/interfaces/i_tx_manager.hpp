// include/internface/i_tx_manager.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "espnow_types.hpp"

/**
 * @interface ITxManager
 * @brief Manager for transmission queue and background sending task (internal)
 * @internal
 */
class ITxManager
{
public:
    virtual ~ITxManager() = default;

    /** @internal */
    virtual esp_err_t init(uint32_t stack_size, UBaseType_t priority) = 0;

    /** @internal */
    virtual esp_err_t deinit() = 0;

    /** @internal */
    virtual esp_err_t queue_packet(const TxPacket &packet) = 0;

    /** @internal */
    virtual void notify_physical_fail() = 0;

    /** @internal */
    virtual void notify_link_alive() = 0;

    /** @internal */
    virtual void notify_logical_ack() = 0;

    /** @internal */
    virtual TaskHandle_t get_task_handle() const = 0;
};