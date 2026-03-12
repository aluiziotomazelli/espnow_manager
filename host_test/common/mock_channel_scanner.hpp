// mock_channel_scanner.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_channel_scanner.hpp"

class MockChannelScanner : public IChannelScanner
{
public:
    MOCK_METHOD(ScanResult, scan, (uint8_t start_channel), (override));
    MOCK_METHOD(void, update_node_info, (NodeId id, NodeType type), (override));
};