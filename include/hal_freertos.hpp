// include/freertos_hal.hpp
#pragma once

#include "i_hal_freertos.hpp"
namespace espnow {

/**
 * @brief Hardware Abstraction Layer for FreeRTOS drivers
 * @internal
 */
class FreeRTOSHAL : public IFreeRTOSHAL
{
public:
    FreeRTOSHAL() = default;

    // Task
    void task_delay(TickType_t xTicksToWait) override { vTaskDelay(xTicksToWait); }
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
    void task_suspend(TaskHandle_t task_handle) override { vTaskSuspend(task_handle); }
    BaseType_t task_notify(TaskHandle_t task_handle, uint32_t bits, eNotifyAction action) override
    {
        return xTaskNotify(task_handle, bits, action);
    }
    BaseType_t
    task_notify_wait(uint32_t bits_clear_entry, uint32_t bits_clear_exit, uint32_t *value, TickType_t xTicksToWait)
        override
    {
        return xTaskNotifyWait(bits_clear_entry, bits_clear_exit, value, xTicksToWait);
    }

    // Queue
    QueueHandle_t queue_create(UBaseType_t length, UBaseType_t item_size) override
    {
        return xQueueCreate(length, item_size);
    }
    void queue_delete(QueueHandle_t queue_handle) override { vQueueDelete(queue_handle); }
    BaseType_t queue_send(QueueHandle_t queue_handle, const void *data, TickType_t xTicksToWait) override
    {
        return xQueueSend(queue_handle, data, xTicksToWait);
    }
    BaseType_t queue_receive(QueueHandle_t queue_handle, void *data, TickType_t xTicksToWait) override
    {
        return xQueueReceive(queue_handle, data, xTicksToWait);
    }
    BaseType_t
    queue_send_fromISR(QueueHandle_t queue_handle, const void *data, BaseType_t *pxHigherPriorityTaskWoken) override
    {
        return xQueueSendFromISR(queue_handle, data, pxHigherPriorityTaskWoken);
    }

    // Timer
    TimerHandle_t timer_create(
        const char *name,
        TickType_t xTimerPeriodInTicks,
        UBaseType_t auto_reload,
        void *id,
        TimerCallbackFunction_t callback) override
    {
        return xTimerCreate(name, xTimerPeriodInTicks, auto_reload, id, callback);
    }
    BaseType_t timer_start(TimerHandle_t timer_handle, TickType_t xTicksToWait) override
    {
        return xTimerStart(timer_handle, xTicksToWait);
    }
    BaseType_t timer_stop(TimerHandle_t timer_handle, TickType_t xTicksToWait) override
    {
        return xTimerStop(timer_handle, xTicksToWait);
    }
    BaseType_t timer_delete(TimerHandle_t timer_handle, TickType_t xTicksToWait) override
    {
        return xTimerDelete(timer_handle, xTicksToWait);
    }
    void *timer_get_id(TimerHandle_t timer_handle) override { return pvTimerGetTimerID(timer_handle); }

    // Mutex
    SemaphoreHandle_t mutex_create() override { return xSemaphoreCreateMutex(); }
    SemaphoreHandle_t semaphore_create_binary() override { return xSemaphoreCreateBinary(); }
    BaseType_t semaphore_take(SemaphoreHandle_t semaphore_handle, TickType_t xTicksToWait) override
    {
        return xSemaphoreTake(semaphore_handle, xTicksToWait);
    }
    BaseType_t semaphore_give(SemaphoreHandle_t semaphore_handle) override { return xSemaphoreGive(semaphore_handle); }
    void semaphore_delete(SemaphoreHandle_t semaphore_handle) override { vSemaphoreDelete(semaphore_handle); }
};
} // namespace espnow
