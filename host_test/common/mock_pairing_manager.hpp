// host_test/common/mock_pairing_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_pairing_manager.hpp"

class MockPairingManager : public IPairingManager
{
public:
    MOCK_METHOD(esp_err_t, init, (NodeId id, NodeType type), (override));
    MOCK_METHOD(esp_err_t, start, (uint32_t timeout_ms, uint64_t now_ms), (override));
    MOCK_METHOD(void, set_channel, (uint8_t channel), (override));
    MOCK_METHOD(bool, is_active, (), (const, override));
    MOCK_METHOD(void, tick, (const uint64_t now_ms), (override));
    MOCK_METHOD(void, handle_request, (const RxPacket &packet), (override));
    MOCK_METHOD(void, handle_response, (const RxPacket &packet), (override));
};
