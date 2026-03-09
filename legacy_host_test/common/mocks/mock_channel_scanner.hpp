#pragma once

#include "espnow_interfaces.hpp"
#include <queue>

class MockChannelScanner : public IChannelScanner
{
public:
    // --- Stubbing variables (Control behavior) ---
    ScanResult scan_ret = {1, false};
    std::queue<ScanResult> scan_responses;

    // --- Spying variables (Verify calls) ---
    int scan_calls = 0;
    int update_node_info_calls = 0;

    uint8_t last_start_channel = 0;
    NodeId last_node_id = 0;
    NodeType last_node_type = 0;

    // --- Interface Implementation ---

    inline ScanResult scan(uint8_t start_channel) override
    {
        scan_calls++;
        last_start_channel = start_channel;

        if (scan_responses.empty()) return scan_ret;

        ScanResult res = scan_responses.front();
        scan_responses.pop();
        return res;
    }

    inline void update_node_info(NodeId id, NodeType type) override
    {
        update_node_info_calls++;
        last_node_id = id;
        last_node_type = type;
    }

    void reset()
    {
        scan_calls = 0;
        update_node_info_calls = 0;
        last_start_channel = 0;
        last_node_id = 0;
        last_node_type = 0;
        scan_ret = {1, false};
        while (!scan_responses.empty()) scan_responses.pop();
    }
};
