// host_test/common/mock_peer_manager.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_peer_manager.hpp"

class MockPeerManager : public IPeerManager
{
public:
    MOCK_METHOD(esp_err_t, add, (NodeId, const uint8_t *, NodeType, uint32_t), (override));
    MOCK_METHOD(esp_err_t, remove, (NodeId), (override));
    MOCK_METHOD(bool, find_mac, (NodeId, uint8_t *), (override));
    MOCK_METHOD(std::vector<PeerInfo>, get_all, (), (override));
    MOCK_METHOD(std::vector<NodeId>, get_offline, (uint64_t), (override));
    MOCK_METHOD(void, update_last_seen, (NodeId, uint64_t), (override));
    MOCK_METHOD(esp_err_t, load_from_storage, (uint8_t &), (override));
    MOCK_METHOD(void, persist, (), (override));
    MOCK_METHOD(void, set_channel, (uint8_t), (override));
};