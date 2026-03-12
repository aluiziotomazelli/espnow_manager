// include/interfaces/i_freertos_hal.hpp
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

/**
 * @interface IFreertosHAL
 * @brief Hardware Abstraction Layer for FreeRTOS drivers (internal)
 * @internal
 */
class IFreertosHAL
{
public:
    virtual ~IFreertosHAL() = default;

    // Task
    virtual void hal_task_delay(uint32_t delay_ms) = 0;
    virtual TaskHandle_t get_task_handle() = 0;
    virtual BaseType_t task_create(
        TaskFunction_t pvTaskCode,
        const char *const pcName,
        const uint32_t usStackDepth,
        void *const pvParameters,
        UBaseType_t uxPriority,
        TaskHandle_t *const pxCreatedTask) = 0;
    virtual void task_delete(TaskHandle_t task_handle);
    virtual void task_notify(TaskHandle_t task_handle, uint32_t bits, eNotifyAction action);
    virtual BaseType_t
    task_notify_wait(uint32_t bits_clear_entry, uint32_t bits_clear_exit, uint32_t *value, uint32_t timeout_ms);

    // Queue
    virtual QueueHandle_t queue_create(UBaseType_t length, UBaseType_t item_size) = 0;
    virtual void queue_delete(QueueHandle_t queue_handle) = 0;
    virtual BaseType_t queue_send(QueueHandle_t queue_handle, const void *data, uint32_t timeout_ms) = 0;
    virtual BaseType_t queue_receive(QueueHandle_t queue_handle, void *data, uint32_t timeout_ms) = 0;

    // Timer
    virtual TimerHandle_t timer_create(
        const char *name,
        uint32_t period_ms,
        UBaseType_t auto_reload,
        void *id,
        TimerCallbackFunction_t callback) = 0;
    virtual BaseType_t timer_start(TimerHandle_t timer_handle, uint32_t timeout_ms) = 0;
    virtual BaseType_t timer_stop(TimerHandle_t timer_handle, uint32_t timeout_ms) = 0;
    virtual void timer_delete(TimerHandle_t timer_handle, uint32_t timeout_ms) = 0;
};