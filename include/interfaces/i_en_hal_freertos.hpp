// include/interfaces/i_freertos_hal.hpp
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

namespace espnow {
static constexpr uint32_t PORT_MAX_DELAY = 0xffffffffUL; ///< Equivalent to portMAX_DELAY — wait forever

/**
 * @interface IFreeRTOSHAL
 * @brief Hardware Abstraction Layer for FreeRTOS.
 * @internal
 */
class IFreeRTOSHAL
{
public:
    virtual ~IFreeRTOSHAL() = default;

    // Task
    /** @copydoc vTaskDelay() */
    virtual void task_delay(TickType_t xTicksToWait) = 0;

    /** @copydoc xTaskGetCurrentTaskHandle() */
    virtual TaskHandle_t get_task_handle() = 0;

    /** @copydoc xTaskCreate() */
    virtual BaseType_t task_create(
        TaskFunction_t pvTaskCode,
        const char* const pcName,
        const uint32_t usStackDepth,
        void* const pvParameters,
        UBaseType_t uxPriority,
        TaskHandle_t* const pxCreatedTask) = 0;

    /** @copydoc vTaskDelete() */
    virtual void task_delete(TaskHandle_t task_handle) = 0;

    /** @copydoc vTaskSuspend() */
    virtual void task_suspend(TaskHandle_t task_handle) = 0;

    /** @copydoc xTaskNotify() */
    virtual BaseType_t task_notify(TaskHandle_t task_handle, uint32_t bits, eNotifyAction action) = 0;

    /** @copydoc xTaskNotifyWait() */
    virtual BaseType_t
    task_notify_wait(uint32_t bits_clear_entry, uint32_t bits_clear_exit, uint32_t* value, TickType_t xTicksToWait) = 0;

    // Queue
    /** @copydoc xQueueCreate() */
    virtual QueueHandle_t queue_create(UBaseType_t length, UBaseType_t item_size) = 0;

    /** @copydoc vQueueDelete() */
    virtual void queue_delete(QueueHandle_t queue_handle) = 0;

    /** @copydoc xQueueSend() */
    virtual BaseType_t queue_send(QueueHandle_t queue_handle, const void* data, TickType_t xTicksToWait) = 0;

    /** @copydoc xQueueReceive() */
    virtual BaseType_t queue_receive(QueueHandle_t queue_handle, void* data, TickType_t xTicksToWait) = 0;

    /** @copydoc xQueueSendFromISR() */
    virtual BaseType_t
    queue_send_fromISR(QueueHandle_t queue_handle, const void* data, BaseType_t* pxHigherPriorityTaskWoken) = 0;

    // Timer
    /** @copydoc xTimerCreate() */
    virtual TimerHandle_t timer_create(
        const char* name,
        TickType_t xTimerPeriodInTicks,
        UBaseType_t auto_reload,
        void* id,
        TimerCallbackFunction_t callback) = 0;

    /** @copydoc xTimerStart() */
    virtual BaseType_t timer_start(TimerHandle_t timer_handle, TickType_t xTicksToWait) = 0;

    /** @copydoc xTimerStop() */
    virtual BaseType_t timer_stop(TimerHandle_t timer_handle, TickType_t xTicksToWait) = 0;

    /** @copydoc xTimerDelete() */
    virtual BaseType_t timer_delete(TimerHandle_t timer_handle, TickType_t xTicksToWait) = 0;

    /** @copydoc pvTimerGetTimerID() */
    virtual void* timer_get_id(TimerHandle_t timer_handle) = 0;

    // Mutex and Semaphore
    /** @copydoc xSemaphoreCreateMutex() */
    virtual SemaphoreHandle_t mutex_create() = 0;

    /** @copydoc xSemaphoreCreateBinary() */
    virtual SemaphoreHandle_t semaphore_create_binary() = 0;

    /** @copydoc xSemaphoreTake() */
    virtual BaseType_t semaphore_take(SemaphoreHandle_t semaphore_handle, TickType_t xTicksToWait) = 0;

    /** @copydoc xSemaphoreGive() */
    virtual BaseType_t semaphore_give(SemaphoreHandle_t semaphore_handle) = 0;

    /** @copydoc vSemaphoreDelete() */
    virtual void semaphore_delete(SemaphoreHandle_t semaphore_handle) = 0;

    /** @copydoc xEventGroupCreate() */
    virtual EventGroupHandle_t event_group_create() = 0;

    /** @copydoc xEventGroupSetBits() */
    virtual EventBits_t event_group_set_bits(EventGroupHandle_t eg, EventBits_t bits) = 0;

    /** @copydoc xEventGroupWaitBits() */
    virtual EventBits_t event_group_wait_bits(
        EventGroupHandle_t eg,
        EventBits_t bits_to_wait,
        BaseType_t clear_on_exit,
        BaseType_t wait_all_bits,
        TickType_t ticks_to_wait) = 0;

    /** @copydoc vEventGroupDelete() */
    virtual void event_group_delete(EventGroupHandle_t eg) = 0;
};
} // namespace espnow
