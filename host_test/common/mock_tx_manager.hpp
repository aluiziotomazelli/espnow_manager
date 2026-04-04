// host_test/common/mock_tx_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_tx_manager.hpp"

/**
 * @class MockTxManager
 * @brief GoogleMock implementation of ITxManager.
 */
class MockTxManager : public ITxManager
{
public:
    MOCK_METHOD(esp_err_t, init, (uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle), (override));
    MOCK_METHOD(void, deinit, (), (override));
    MOCK_METHOD(esp_err_t, queue_packet, (const DecodedTxPacket& packet), (override));
    MOCK_METHOD(void, notify_delivery_failure, (), (override));
    MOCK_METHOD(void, notify_delivery_success, (), (override));
    MOCK_METHOD(void, notify_link_alive, (), (override));
    MOCK_METHOD(void, handle_ack, (const DecodedRxPacket& decoded), (override));
    MOCK_METHOD(TaskHandle_t, get_task_handle, (), (const, override));
};
