#pragma once

#include "espnow_interfaces.hpp"

class MockChannelScanner : public IChannelScanner
{
public:
    inline ScanResult scan(uint8_t start_channel) override
    {
        return {start_channel, false};
    }
    inline void update_node_info(NodeId id, NodeType type) override {}
};
