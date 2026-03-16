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
        (NodeId id, NodeType type, ITxManager *tx_mgr, IChannelObserver *observer),
        (override));
    MOCK_METHOD(ScanResult, scan, (), (override));
    MOCK_METHOD(void, handle_probe, (const RxPacket &packet), (override));
    MOCK_METHOD(void, set_channel, (uint8_t channel), (override));
};
