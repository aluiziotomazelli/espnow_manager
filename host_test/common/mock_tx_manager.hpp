// mock_tx_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_tx_manager.hpp"

class MockTxManager : public ITxManager
{
public:
    MOCK_METHOD(esp_err_t, init, (uint32_t stack_size, UBaseType_t priority), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(esp_err_t, queue_packet, (const TxPacket &packet), (override));
    MOCK_METHOD(void, notify_physical_fail, (), (override));
    MOCK_METHOD(void, notify_link_alive, (), (override));
    MOCK_METHOD(void, notify_logical_ack, (), (override));
    MOCK_METHOD(TaskHandle_t, get_task_handle, (), (const, override));
};