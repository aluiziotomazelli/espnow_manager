// host_test/common/mock_pairing_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_pairing_manager.hpp"

class MockPairingManager : public IPairingManager
{
public:
    MOCK_METHOD(esp_err_t, init, (NodeId id, NodeType type, TaskHandle_t rx_task_handle), (override));
    MOCK_METHOD(esp_err_t, start, (uint32_t timeout_ms, uint64_t now_ms), (override));
    MOCK_METHOD(void, tick, (uint64_t now_ms), (override));
    MOCK_METHOD(void, handle_request, (const DecodedPacket& decoded), (override));
    MOCK_METHOD(void, handle_response, (const DecodedPacket& decoded), (override));
};
