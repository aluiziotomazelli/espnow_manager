#pragma once

#include "espnow_interfaces.hpp"
#include <vector>

class MockPeerManager : public IPeerManager
{
public:
    inline esp_err_t add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type, uint32_t heartbeat_interval_ms = 0) override
    {
        return ESP_OK;
    }
    inline esp_err_t remove(NodeId id) override
    {
        return ESP_OK;
    }
    inline bool find_mac(NodeId id, uint8_t *mac) override
    {
        return false;
    }
    inline std::vector<PeerInfo> get_all() override
    {
        return {};
    }
    inline std::vector<NodeId> get_offline(uint64_t now_ms) override
    {
        return {};
    }
    inline void update_last_seen(NodeId id, uint64_t now_ms) override
    {
    }
    inline esp_err_t load_from_storage(uint8_t &wifi_channel) override
    {
        return ESP_OK;
    }
    inline void persist(uint8_t wifi_channel) override
    {
    }
};
