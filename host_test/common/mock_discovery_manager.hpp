// mock_discovery_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_discovery_manager.hpp"

class MockDiscoveryManager : public IDiscoveryManager
{
public:
    MOCK_METHOD(
        esp_err_t,
        init,
        (NodeId id, NodeType type, TaskHandle_t rx_task_handle, UBaseType_t priority, uint32_t stack_size),
        (override));
    MOCK_METHOD(void, deinit, (), (override));
    MOCK_METHOD(void, start_scan, (), (override));
    MOCK_METHOD(void, stop_scan, (), (override));
    MOCK_METHOD(bool, is_scanning, (), (const, override));

    MOCK_METHOD(void, handle_scan_probe, (const DecodedRxPacket& decoded), (override));
    MOCK_METHOD(void, set_channel, (uint8_t channel), (override));
    MOCK_METHOD(uint8_t, get_channel, (), (const, override));
};
