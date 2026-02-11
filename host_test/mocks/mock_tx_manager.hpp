#pragma once

#include "espnow_interfaces.hpp"

class MockTxManager : public ITxManager
{
public:
    inline esp_err_t init(uint32_t stack_size, UBaseType_t priority) override { return ESP_OK; }
    inline esp_err_t deinit() override { return ESP_OK; }
    inline esp_err_t queue_packet(const TxPacket &packet) override { return ESP_OK; }
    inline void notify_physical_fail() override {}
    inline void notify_link_alive() override {}
    inline void notify_logical_ack() override {}
    inline void notify_hub_found() override {}
    inline TaskHandle_t get_task_handle() const override { return nullptr; }
};
