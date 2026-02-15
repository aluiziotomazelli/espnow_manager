#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "espnow_interfaces.hpp"

class RealWiFiHAL : public IWiFiHAL
{
public:
    RealWiFiHAL();

    void set_task_to_notify(TaskHandle_t task_handle) override
    {
        task_handle_ = task_handle;
    }

    esp_err_t set_channel(uint8_t channel) override;
    esp_err_t get_channel(uint8_t *channel) override;
    esp_err_t send_packet(const uint8_t *mac, const uint8_t *data, size_t len) override;
    bool wait_for_event(uint32_t event_mask, uint32_t timeout_ms) override;

private:
    TaskHandle_t task_handle_;
};
