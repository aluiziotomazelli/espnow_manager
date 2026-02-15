#pragma once

#include "espnow_interfaces.hpp"
#include <cstring>

class MockHeartbeatManager : public IHeartbeatManager
{
public:
    // --- Stubbing variables ---
    esp_err_t init_ret = ESP_OK;
    esp_err_t deinit_ret = ESP_OK;

    // --- Spying variables ---
    int init_calls = 0;
    int deinit_calls = 0;
    int update_node_id_calls = 0;
    int handle_response_calls = 0;
    int handle_request_calls = 0;

    NodeId last_node_id = 0;
    NodeId last_hub_id = 0;
    uint8_t last_channel = 0;
    NodeId last_sender_id = 0;
    uint8_t last_sender_mac[6] = {0};
    uint64_t last_uptime_ms = 0;
    uint32_t last_interval_ms = 0;
    NodeType last_type = 0;

    // --- Interface Implementation ---

    inline esp_err_t init(uint32_t interval_ms, NodeType type) override
    {
        init_calls++;
        last_interval_ms = interval_ms;
        last_type = type;
        return init_ret;
    }

    inline void update_node_id(NodeId id) override
    {
        update_node_id_calls++;
        last_node_id = id;
    }

    inline esp_err_t deinit() override
    {
        deinit_calls++;
        return deinit_ret;
    }

    inline void handle_response(NodeId hub_id, uint8_t channel) override
    {
        handle_response_calls++;
        last_hub_id = hub_id;
        last_channel = channel;
    }

    inline void handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms) override
    {
        handle_request_calls++;
        last_sender_id = sender_id;
        if (mac) memcpy(last_sender_mac, mac, 6);
        last_uptime_ms = uptime_ms;
    }

    void reset()
    {
        init_calls = 0;
        deinit_calls = 0;
        update_node_id_calls = 0;
        handle_response_calls = 0;
        handle_request_calls = 0;

        last_node_id = 0;
        last_hub_id = 0;
        last_channel = 0;
        last_sender_id = 0;
        memset(last_sender_mac, 0, 6);
        last_uptime_ms = 0;
        last_interval_ms = 0;
        last_type = 0;

        init_ret = ESP_OK;
        deinit_ret = ESP_OK;
    }
};
