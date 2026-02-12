#pragma once

#include "espnow_interfaces.hpp"

class MockMessageRouter : public IMessageRouter
{
public:
    inline void handle_packet(const RxPacket &packet) override {}
    inline bool should_dispatch_to_worker(MessageType type) override { return false; }
    inline void set_app_queue(QueueHandle_t app_queue) override {}
    inline void set_node_info(NodeId id, NodeType type) override {}
};
