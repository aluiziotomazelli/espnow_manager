// mock_message_router.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_message_router.hpp"

class MockMessageRouter : public IMessageRouter
{
public:
    MOCK_METHOD(void, handle_packet, (const RxPacket &packet), (override));
    MOCK_METHOD(bool, should_dispatch_to_worker, (MessageType type), (override));
    MOCK_METHOD(void, set_app_queue, (QueueHandle_t app_queue), (override));
    MOCK_METHOD(void, set_node_info, (NodeId id, NodeType type), (override));
};

// Note: Template method set_node_info with enum is implemented in the
// base interface and redirects to the mocked method