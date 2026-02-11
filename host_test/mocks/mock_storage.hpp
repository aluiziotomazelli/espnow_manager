#pragma once

#include "espnow_interfaces.hpp"
#include <vector>

class MockStorage : public IStorage
{
public:
    uint8_t saved_channel = 0;
    std::vector<PersistentPeer> saved_peers;
    bool save_called = false;

    inline esp_err_t load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers) override
    {
        wifi_channel = saved_channel;
        peers = saved_peers;
        return ESP_OK;
    }

    inline esp_err_t save(uint8_t wifi_channel, const std::vector<PersistentPeer> &peers, bool force_nvs_commit) override
    {
        saved_channel = wifi_channel;
        saved_peers = peers;
        save_called = true;
        return ESP_OK;
    }
};
