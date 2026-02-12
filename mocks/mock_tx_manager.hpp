#pragma once

#include "espnow_interfaces.hpp"
#include <vector>

class MockTxManager : public ITxManager
{
public:
    // --- Stubbing variables (Control behavior) ---
    esp_err_t init_ret = ESP_OK;
    esp_err_t queue_packet_ret = ESP_OK;

    // --- Spying variables (Verify calls) ---
    int init_calls = 0;
    int deinit_calls = 0;
    int queue_packet_calls = 0;
    int notify_physical_fail_calls = 0;
    int notify_link_alive_calls = 0;
    int notify_logical_ack_calls = 0;
    int notify_hub_found_calls = 0;

    TxPacket last_queued_packet = {};
    uint32_t last_stack_size = 0;
    UBaseType_t last_priority = 0;
    TaskHandle_t fake_handle = nullptr;

    // --- Interface Implementation ---

    inline esp_err_t init(uint32_t stack_size, UBaseType_t priority) override
    {
        init_calls++;
        last_stack_size = stack_size;
        last_priority = priority;
        return init_ret;
    }

    inline esp_err_t deinit() override
    {
        deinit_calls++;
        return ESP_OK;
    }

    inline esp_err_t queue_packet(const TxPacket &packet) override
    {
        queue_packet_calls++;
        last_queued_packet = packet;
        return queue_packet_ret;
    }

    inline void notify_physical_fail() override { notify_physical_fail_calls++; }
    inline void notify_link_alive() override { notify_link_alive_calls++; }
    inline void notify_logical_ack() override { notify_logical_ack_calls++; }
    inline void notify_hub_found() override { notify_hub_found_calls++; }

    inline TaskHandle_t get_task_handle() const override { return fake_handle; }

    void reset()
    {
        init_calls = 0;
        deinit_calls = 0;
        queue_packet_calls = 0;
        notify_physical_fail_calls = 0;
        notify_link_alive_calls = 0;
        notify_logical_ack_calls = 0;
        notify_hub_found_calls = 0;
        last_queued_packet = {};
        last_stack_size = 0;
        last_priority = 0;
        fake_handle = nullptr;
        init_ret = ESP_OK;
        queue_packet_ret = ESP_OK;
    }
};
