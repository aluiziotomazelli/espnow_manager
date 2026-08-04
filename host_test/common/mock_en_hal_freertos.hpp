// host_test/common/mock_en_hal_freertos.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_en_hal_freertos.hpp"

namespace espnow {

class MockFreeRTOSHAL : public IFreeRTOSHAL
{
public:
    MOCK_METHOD(void, task_delay, (TickType_t), (override));
    MOCK_METHOD(TaskHandle_t, get_task_handle, (), (override));
    MOCK_METHOD(
        BaseType_t,
        task_create,
        (TaskFunction_t, const char* const, const uint32_t, void* const, UBaseType_t, TaskHandle_t* const),
        (override));
    MOCK_METHOD(void, task_delete, (TaskHandle_t), (override));
    MOCK_METHOD(void, task_suspend, (TaskHandle_t), (override));
    MOCK_METHOD(BaseType_t, task_notify, (TaskHandle_t, uint32_t, eNotifyAction), (override));
    MOCK_METHOD(BaseType_t, task_notify_wait, (uint32_t, uint32_t, uint32_t*, TickType_t), (override));

    MOCK_METHOD(QueueHandle_t, queue_create, (UBaseType_t, UBaseType_t), (override));
    MOCK_METHOD(void, queue_delete, (QueueHandle_t), (override));
    MOCK_METHOD(BaseType_t, queue_send, (QueueHandle_t, const void*, TickType_t), (override));
    MOCK_METHOD(BaseType_t, queue_receive, (QueueHandle_t, void*, TickType_t), (override));
    MOCK_METHOD(BaseType_t, queue_send_fromISR, (QueueHandle_t, const void*, BaseType_t*), (override));

    MOCK_METHOD(
        TimerHandle_t,
        timer_create,
        (const char*, TickType_t, UBaseType_t, void*, TimerCallbackFunction_t),
        (override));
    MOCK_METHOD(BaseType_t, timer_start, (TimerHandle_t, TickType_t), (override));
    MOCK_METHOD(BaseType_t, timer_stop, (TimerHandle_t, TickType_t), (override));
    MOCK_METHOD(BaseType_t, timer_delete, (TimerHandle_t, TickType_t), (override));
    MOCK_METHOD(void*, timer_get_id, (TimerHandle_t), (override));

    MOCK_METHOD(SemaphoreHandle_t, mutex_create, (), (override));
    MOCK_METHOD(SemaphoreHandle_t, semaphore_create_binary, (), (override));
    MOCK_METHOD(BaseType_t, semaphore_take, (SemaphoreHandle_t, TickType_t), (override));
    MOCK_METHOD(BaseType_t, semaphore_give, (SemaphoreHandle_t), (override));
    MOCK_METHOD(void, semaphore_delete, (SemaphoreHandle_t), (override));

    MOCK_METHOD(EventGroupHandle_t, event_group_create, (), (override));
    MOCK_METHOD(EventBits_t, event_group_set_bits, (EventGroupHandle_t, EventBits_t), (override));
    MOCK_METHOD(
        EventBits_t,
        event_group_wait_bits,
        (EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t, TickType_t),
        (override));
    MOCK_METHOD(void, event_group_delete, (EventGroupHandle_t), (override));
};

} // namespace espnow
