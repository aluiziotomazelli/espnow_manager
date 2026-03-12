// include/freertos_hal.hpp
#pragma once

#include "interfaces/i_freertos_hal.hpp"

/**
 * @brief Hardware Abstraction Layer for FreeRTOS drivers
 * @internal
 */
class FreeRTOSHAL : public IFreertosHAL
{
    void task_delay(uint32_t delay_ms) override { vTaskDelay(pdMS_TO_TICKS(delay_ms)); }
    TaskHandle_t get_task_handle() override { return xTaskGetCurrentTaskHandle(); }
    BaseType_t task_create(
        TaskFunction_t pvTaskCode,
        const char *const pcName,
        const uint32_t usStackDepth,
        void *const pvParameters,
        UBaseType_t uxPriority,
        TaskHandle_t *const pxCreatedTask) override
    {
        return xTaskCreate(pvTaskCode, pcName, usStackDepth, pvParameters, uxPriority, pxCreatedTask);
    }
    void task_delete(TaskHandle_t task_handle) override { vTaskDelete(task_handle); }
    void task_notify(TaskHandle_t task_handle, uint32_t bits, eNotifyAction action) override
    {
        xTaskNotify(task_handle, bits, action);
    }
    BaseType_t
    task_notify_wait(uint32_t bits_clear_entry, uint32_t bits_clear_exit, uint32_t *value, uint32_t timeout_ms) override
    {
        return xTaskNotifyWait(bits_clear_entry, bits_clear_exit, value, pdMS_TO_TICKS(timeout_ms));
    }

    // Queue
    QueueHandle_t queue_create(UBaseType_t length, UBaseType_t item_size) override
    {
        return xQueueCreate(length, item_size);
    }
    void queue_delete(QueueHandle_t queue_handle) override { vQueueDelete(queue_handle); }
    BaseType_t queue_send(QueueHandle_t queue_handle, const void *data, uint32_t timeout_ms) override
    {
        return xQueueSend(queue_handle, data, pdMS_TO_TICKS(timeout_ms));
    }
    BaseType_t queue_receive(QueueHandle_t queue_handle, void *data, uint32_t timeout_ms) override
    {
        return xQueueReceive(queue_handle, data, pdMS_TO_TICKS(timeout_ms));
    }

    // Timer
    TimerHandle_t timer_create(
        const char *name,
        uint32_t period_ms,
        UBaseType_t auto_reload,
        void *id,
        TimerCallbackFunction_t callback) override
    {
        return xTimerCreate(name, period_ms, auto_reload, id, nullptr);
    }
    BaseType_t timer_start(TimerHandle_t timer_handle, uint32_t timeout_ms) override
    {
        return xTimerStart(timer_handle, pdMS_TO_TICKS(timeout_ms));
    }
    BaseType_t timer_stop(TimerHandle_t timer_handle, uint32_t timeout_ms) override
    {
        return xTimerStop(timer_handle, pdMS_TO_TICKS(timeout_ms));
    }
    void timer_delete(TimerHandle_t timer_handle, uint32_t timeout_ms) override
    {
        xTimerDelete(timer_handle, pdMS_TO_TICKS(timeout_ms));
    }
};