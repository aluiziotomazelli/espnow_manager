// host_test/common/mock_peer_manager.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_peer_manager.hpp"

// Note: Template methods (add, remove, find_mac, update_last_seen with enum)
// are implemented in the base interface and redirect to the mocked methods

class MockPeerManager : public IPeerManager
{
public:
    MOCK_METHOD(
        esp_err_t,
        add,
        (NodeId id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms),
        (override));
    MOCK_METHOD(esp_err_t, remove, (NodeId id), (override));
    MOCK_METHOD(bool, find_mac, (NodeId id, uint8_t *mac), (override));
    MOCK_METHOD((etl::vector<PeerInfo, MAX_PEERS>), get_all, (), (override));
    MOCK_METHOD((etl::vector<NodeId, MAX_PEERS>), get_offline, (int64_t now_ms), (override));
    MOCK_METHOD(void, update_last_seen, (NodeId id, int64_t now_ms), (override));
    MOCK_METHOD(esp_err_t, load_peers_from_storage, (), (override));
};
