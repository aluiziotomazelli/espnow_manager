#pragma once

#include "espnow_interfaces.hpp"

class MockPairingManager : public IPairingManager
{
public:
    inline esp_err_t init(NodeType type, NodeId id) override { return ESP_OK; }
    inline esp_err_t deinit() override { return ESP_OK; }
    inline esp_err_t start(uint32_t timeout_ms) override { return ESP_OK; }
    inline bool is_active() const override { return false; }
    inline void handle_request(const RxPacket &packet) override {}
    inline void handle_response(const RxPacket &packet) override {}
};
