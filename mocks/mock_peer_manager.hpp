#pragma once

#include "espnow_interfaces.hpp"
#include <vector>
#include <algorithm>
#include <cstring>

class MockPeerManager : public IPeerManager
{
public:
    // --- Stubbing variables ---
    esp_err_t add_ret = ESP_OK;
    esp_err_t remove_ret = ESP_OK;
    bool find_mac_ret = false;
    std::vector<PeerInfo> get_all_ret;
    std::vector<NodeId> get_offline_ret;
    esp_err_t load_from_storage_ret = ESP_OK;

    // --- Spying variables ---
    int add_calls = 0;
    int remove_calls = 0;
    int find_mac_calls = 0;
    int get_all_calls = 0;
    int get_offline_calls = 0;
    int update_last_seen_calls = 0;
    int load_from_storage_calls = 0;
    int persist_calls = 0;

    NodeId last_add_id = 0;
    uint8_t last_add_mac[6] = {0};
    uint8_t last_add_channel = 0;
    NodeType last_add_type = 0;
    uint32_t last_add_heartbeat_interval_ms = 0;

    NodeId last_remove_id = 0;
    NodeId last_find_mac_id = 0;
    NodeId last_update_last_seen_id = 0;
    uint64_t last_update_last_seen_now_ms = 0;
    uint8_t last_persist_wifi_channel = 0;

    // --- Interface Implementation ---

    inline esp_err_t add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type, uint32_t heartbeat_interval_ms = 0) override
    {
        add_calls++;
        last_add_id = id;
        if (mac) memcpy(last_add_mac, mac, 6);
        last_add_channel = channel;
        last_add_type = type;
        last_add_heartbeat_interval_ms = heartbeat_interval_ms;
        return add_ret;
    }

    inline esp_err_t remove(NodeId id) override
    {
        remove_calls++;
        last_remove_id = id;
        return remove_ret;
    }

    inline bool find_mac(NodeId id, uint8_t *mac) override
    {
        find_mac_calls++;
        last_find_mac_id = id;
        if (find_mac_ret && mac) memcpy(mac, last_add_mac, 6);
        return find_mac_ret;
    }

    inline std::vector<PeerInfo> get_all() override
    {
        get_all_calls++;
        return get_all_ret;
    }

    inline std::vector<NodeId> get_offline(uint64_t now_ms) override
    {
        get_offline_calls++;
        return get_offline_ret;
    }

    inline void update_last_seen(NodeId id, uint64_t now_ms) override
    {
        update_last_seen_calls++;
        last_update_last_seen_id = id;
        last_update_last_seen_now_ms = now_ms;
    }

    inline esp_err_t load_from_storage(uint8_t &wifi_channel) override
    {
        load_from_storage_calls++;
        return load_from_storage_ret;
    }

    inline void persist(uint8_t wifi_channel) override
    {
        persist_calls++;
        last_persist_wifi_channel = wifi_channel;
    }

    void reset()
    {
        add_calls = 0;
        remove_calls = 0;
        find_mac_calls = 0;
        get_all_calls = 0;
        get_offline_calls = 0;
        update_last_seen_calls = 0;
        load_from_storage_calls = 0;
        persist_calls = 0;

        add_ret = ESP_OK;
        remove_ret = ESP_OK;
        find_mac_ret = false;
        get_all_ret.clear();
        get_offline_ret.clear();
        load_from_storage_ret = ESP_OK;

        last_add_id = 0;
        memset(last_add_mac, 0, 6);
        last_add_channel = 0;
        last_add_type = 0;
        last_add_heartbeat_interval_ms = 0;
        last_remove_id = 0;
        last_find_mac_id = 0;
        last_update_last_seen_id = 0;
        last_update_last_seen_now_ms = 0;
        last_persist_wifi_channel = 0;
    }
};
