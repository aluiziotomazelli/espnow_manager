// mock_pairing_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "interface/i_pairing_manager.hpp"

class MockPairingManager : public IPairingManager
{
public:
    MOCK_METHOD(esp_err_t, init, (NodeType type, NodeId id), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(esp_err_t, start, (uint32_t timeout_ms), (override));
    MOCK_METHOD(void, set_channel, (uint8_t channel), (override));
    MOCK_METHOD(bool, is_active, (), (const, override));
    MOCK_METHOD(void, handle_request, (const RxPacket &packet), (override));
    MOCK_METHOD(void, handle_response, (const RxPacket &packet), (override));
};

// Note: Template method init with enum is implemented in the base interface
// and redirects to the mocked method