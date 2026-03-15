// mock_discovery_manager.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_discovery_manager.hpp"

class MockDiscoveryManager : public IDiscoveryManager
{
public:
    MOCK_METHOD(
        esp_err_t,
        init,
        (NodeId id, NodeType type, ITxManager &tx_mgr, IChannelObserver *observer),
        (override));
    MOCK_METHOD(ScanResult, scan, (uint8_t start_channel), (override));
    MOCK_METHOD(void, handle_probe, (const RxPacket &packet), (override));
};
