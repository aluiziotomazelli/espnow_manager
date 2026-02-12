#pragma once

#include "espnow_interfaces.hpp"

class MockHeartbeatManager : public IHeartbeatManager
{
public:
    inline esp_err_t init(uint32_t interval_ms, NodeType type) override { return ESP_OK; }
    inline void update_node_id(NodeId id) override {}
    inline esp_err_t deinit() override { return ESP_OK; }
    inline void handle_response(NodeId hub_id, uint8_t channel) override {}
    inline void handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms) override {}
};
