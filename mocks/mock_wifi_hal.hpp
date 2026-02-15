#pragma once

#include "espnow_interfaces.hpp"
#include <vector>
#include <cstring>
#include <queue>

class MockWiFiHAL : public IWiFiHAL
{
public:
    uint8_t current_channel = 1;
    esp_err_t set_channel_ret = ESP_OK;
    esp_err_t get_channel_ret = ESP_OK;
    esp_err_t send_packet_ret = ESP_OK;
    bool wait_for_event_ret = true;

    int set_channel_calls = 0;
    int get_channel_calls = 0;
    int send_packet_calls = 0;
    int wait_for_event_calls = 0;
    int set_task_to_notify_calls = 0;

    uint8_t last_set_channel = 0;
    uint8_t last_dest_mac[6] = {0};
    std::vector<uint8_t> last_sent_data;
    uint32_t last_event_mask = 0;
    uint32_t last_timeout_ms = 0;
    TaskHandle_t last_task_handle = nullptr;
    std::queue<bool> event_responses;

    inline esp_err_t set_channel(uint8_t channel) override {
        set_channel_calls++;
        last_set_channel = channel;
        current_channel = channel;
        return set_channel_ret;
    }

    inline esp_err_t get_channel(uint8_t *channel) override {
        get_channel_calls++;
        if (channel) *channel = current_channel;
        return get_channel_ret;
    }

    inline esp_err_t send_packet(const uint8_t *mac, const uint8_t *data, size_t len) override {
        send_packet_calls++;
        if (mac) memcpy(last_dest_mac, mac, 6);
        if (data) {
            last_sent_data.assign(data, data + len);
        } else {
            last_sent_data.clear();
        }
        return send_packet_ret;
    }

    inline bool wait_for_event(uint32_t event_mask, uint32_t timeout_ms) override {
        wait_for_event_calls++;
        last_event_mask = event_mask;
        last_timeout_ms = timeout_ms;
        if (event_responses.empty()) return wait_for_event_ret;
        bool res = event_responses.front();
        event_responses.pop();
        return res;
    }

    inline void set_task_to_notify(TaskHandle_t task_handle) override {
        set_task_to_notify_calls++;
        last_task_handle = task_handle;
    }

    void reset() {
        set_channel_calls = 0;
        get_channel_calls = 0;
        send_packet_calls = 0;
        wait_for_event_calls = 0;
        set_task_to_notify_calls = 0;
        last_sent_data.clear();
        memset(last_dest_mac, 0, 6);
        current_channel = 1;
        set_channel_ret = ESP_OK;
        get_channel_ret = ESP_OK;
        send_packet_ret = ESP_OK;
        wait_for_event_ret = true;
        last_task_handle = nullptr;
        last_event_mask = 0;
        last_timeout_ms = 0;
        last_set_channel = 0;
        while (!event_responses.empty()) event_responses.pop();
    }
};
