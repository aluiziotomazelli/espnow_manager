// mock_heartbeat_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_heartbeat_manager.hpp"

namespace espnow {

class MockHeartbeatManager : public IHeartbeatManager
{
public:
    MOCK_METHOD(void, init, (NodeId id, NodeType type, uint32_t interval_ms, bool enable_heartbeat), (override));
    MOCK_METHOD(void, deinit, (), (override));
    MOCK_METHOD(void, tick, (int64_t now_ms), (override));
    MOCK_METHOD(void, set_interval_ms, (uint32_t heartbeat_interval_ms), (override));
    MOCK_METHOD(void, set_enable_heartbeat, (bool enable), (override));
    MOCK_METHOD(bool, is_heartbeat_enabled, (), (const, override));
    MOCK_METHOD(void, handle_response, (const DecodedRxPacket& decoded), (override));
    MOCK_METHOD(void, handle_request, (const DecodedRxPacket& decoded), (override));
};

} // namespace espnow
