// include/internface/i_wifi_hal.hpp
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @interface IWiFiHAL
 * @brief Hardware Abstraction Layer for WiFi and ESP-NOW drivers (internal)
 * @internal
 */
class IWiFiHAL
{
public:
    virtual ~IWiFiHAL() = default;

    /** @internal */
    virtual esp_err_t set_channel(uint8_t channel) = 0;
    /** @internal */
    virtual esp_err_t get_channel(uint8_t *channel) = 0;
    /** @internal */
    virtual esp_err_t send_packet(const uint8_t *mac, const uint8_t *data, size_t len) = 0;
    /** @internal */
    virtual bool wait_for_event(uint32_t event_mask, uint32_t timeout_ms) = 0;
    /** @internal */
    virtual void set_task_to_notify(TaskHandle_t task_handle) = 0;
};