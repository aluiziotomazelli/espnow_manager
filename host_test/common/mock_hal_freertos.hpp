// mock_freertos_hal.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_hal_freertos.hpp"

class MockFreeRTOSHAL : public IFreeRTOSHAL
{
public:
    MOCK_METHOD(void, task_delay, (uint32_t delay_ms), (override));
    MOCK_METHOD(TaskHandle_t, get_task_handle, (), (override));
    MOCK_METHOD(
        BaseType_t,
        task_create,
        (TaskFunction_t, const char *const, const uint32_t, void *const, UBaseType_t, TaskHandle_t *const),
        (override));
    MOCK_METHOD(void, task_delete, (TaskHandle_t), (override));
    MOCK_METHOD(void, task_suspend, (TaskHandle_t), (override));
    MOCK_METHOD(void, task_notify, (TaskHandle_t, uint32_t, eNotifyAction), (override));
    MOCK_METHOD(BaseType_t, task_notify_wait, (uint32_t, uint32_t, uint32_t *, uint32_t), (override));

    MOCK_METHOD(QueueHandle_t, queue_create, (UBaseType_t, UBaseType_t), (override));
    MOCK_METHOD(void, queue_delete, (QueueHandle_t), (override));
    MOCK_METHOD(BaseType_t, queue_send, (QueueHandle_t, const void *, uint32_t), (override));
    MOCK_METHOD(BaseType_t, queue_receive, (QueueHandle_t, void *, uint32_t), (override));
    MOCK_METHOD(void *, timer_get_id, (TimerHandle_t), (override));

    MOCK_METHOD(
        TimerHandle_t,
        timer_create,
        (const char *, uint32_t, UBaseType_t, void *, TimerCallbackFunction_t),
        (override));
    MOCK_METHOD(BaseType_t, timer_start, (TimerHandle_t, uint32_t), (override));
    MOCK_METHOD(BaseType_t, timer_stop, (TimerHandle_t, uint32_t), (override));
    MOCK_METHOD(void, timer_delete, (TimerHandle_t, uint32_t), (override));

    MOCK_METHOD(SemaphoreHandle_t, mutex_create, (), (override));
    MOCK_METHOD(SemaphoreHandle_t, semaphore_create_binary, (), (override));
    MOCK_METHOD(BaseType_t, semaphore_take, (SemaphoreHandle_t, uint32_t), (override));
    MOCK_METHOD(BaseType_t, semaphore_give, (SemaphoreHandle_t), (override));
    MOCK_METHOD(void, semaphore_delete, (SemaphoreHandle_t), (override));
};
