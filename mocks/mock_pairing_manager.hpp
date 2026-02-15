#pragma once

#include "espnow_interfaces.hpp"
#include <cstring>

class MockPairingManager : public IPairingManager
{
public:
    // --- Stubbing variables (Control behavior) ---
    esp_err_t init_ret = ESP_OK;
    esp_err_t deinit_ret = ESP_OK;
    esp_err_t start_ret = ESP_OK;
    bool is_active_ret = false;

    // --- Spying variables (Verify calls) ---
    int init_calls = 0;
    int deinit_calls = 0;
    int start_calls = 0;
    mutable int is_active_calls = 0;
    int handle_request_calls = 0;
    int handle_response_calls = 0;

    NodeType last_node_type = 0;
    NodeId last_node_id = 0;
    uint32_t last_timeout_ms = 0;
    RxPacket last_request_packet = {};
    RxPacket last_response_packet = {};

    // Convenience overlays for captured packets
    PairRequest last_request_data = {};
    PairResponse last_response_data = {};

    // --- Interface Implementation ---

    inline esp_err_t init(NodeType type, NodeId id) override
    {
        init_calls++;
        last_node_type = type;
        last_node_id = id;
        return init_ret;
    }

    inline esp_err_t deinit() override
    {
        deinit_calls++;
        return deinit_ret;
    }

    inline esp_err_t start(uint32_t timeout_ms) override
    {
        start_calls++;
        last_timeout_ms = timeout_ms;
        return start_ret;
    }

    inline bool is_active() const override
    {
        is_active_calls++;
        return is_active_ret;
    }

    inline void handle_request(const RxPacket &packet) override
    {
        handle_request_calls++;
        last_request_packet = packet;
        if (packet.len >= sizeof(PairRequest)) {
            memcpy(&last_request_data, packet.data, sizeof(PairRequest));
        }
    }

    inline void handle_response(const RxPacket &packet) override
    {
        handle_response_calls++;
        last_response_packet = packet;
        if (packet.len >= sizeof(PairResponse)) {
            memcpy(&last_response_data, packet.data, sizeof(PairResponse));
        }
    }

    void reset()
    {
        init_calls = 0;
        deinit_calls = 0;
        start_calls = 0;
        is_active_calls = 0;
        handle_request_calls = 0;
        handle_response_calls = 0;

        last_node_type = 0;
        last_node_id = 0;
        last_timeout_ms = 0;
        memset(&last_request_packet, 0, sizeof(RxPacket));
        memset(&last_response_packet, 0, sizeof(RxPacket));
        memset(&last_request_data, 0, sizeof(PairRequest));
        memset(&last_response_data, 0, sizeof(PairResponse));

        init_ret = ESP_OK;
        deinit_ret = ESP_OK;
        start_ret = ESP_OK;
        is_active_ret = false;
    }
};
