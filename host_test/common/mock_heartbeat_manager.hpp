// mock_heartbeat_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_heartbeat_manager.hpp"

class MockHeartbeatManager : public IHeartbeatManager
{
public:
    MOCK_METHOD(esp_err_t, init, (uint32_t interval_ms, NodeType type), (override));
    MOCK_METHOD(void, update_node_id, (NodeId id), (override));
    MOCK_METHOD(esp_err_t, deinit, (), (override));
    MOCK_METHOD(void, handle_response, (NodeId hub_id), (override));
    MOCK_METHOD(void, set_channel, (uint8_t channel), (override));
    MOCK_METHOD(void, handle_request, (const RxPacket &packet), (override));
};

// Note: Template methods (init, update_node_id, handle_response with enum)
// are implemented in the base interface and redirect to the mocked methods