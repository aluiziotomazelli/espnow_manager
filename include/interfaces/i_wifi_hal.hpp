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

    virtual esp_err_t wifi_set_channel(uint8_t channel) = 0;
    virtual esp_err_t wifi_get_channel(uint8_t *channel) = 0;
    virtual esp_err_t wifi_get_mode(wifi_mode_t *mode) = 0;
    virtual esp_err_t wifi_set_channel(uint8_t primary, wifi_second_chan_t second) = 0;
    virtual esp_err_t wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second) = 0;

    virtual esp_err_t hal_esp_now_init() = 0;
    virtual esp_err_t hal_esp_now_deinit() = 0;
    virtual esp_err_t hal_espnow_register_recv_cb(esp_now_recv_cb_t cb) = 0;
    virtual esp_err_t hal_espnow_register_send_cb(esp_now_send_cb_t cb) = 0;
    virtual esp_err_t hal_esp_now_add_peer(const esp_now_peer_info_t *peer) = 0;
    virtual esp_err_t hal_esp_now_mod_peer(const esp_now_peer_info_t *peer) = 0;
    virtual esp_err_t hal_esp_now_del_peer(const uint8_t *peer_addr) = 0;

    virtual esp_err_t hal_esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len) = 0;

    virtual BaseType_t
    hal_task_notify_wait(uint32_t bits_to_clear, uint32_t *notification_value, uint32_t timeout_ms) = 0;

    /**
     * @brief Create a FreeRTOS task.
     * @internal
     * @param pvTaskCode Task entry function.
     * @param pcName Task name.
     * @param usStackDepth Stack depth.
     * @param pvParameters Task parameters.
     * @param uxPriority Task priority.
     * @param pxCreatedTask[out] Output task handle.
     * @return pdPASS on success.
     */
    virtual BaseType_t task_create(
        TaskFunction_t pvTaskCode,
        const char *const pcName,
        const uint32_t usStackDepth,
        void *const pvParameters,
        UBaseType_t uxPriority,
        TaskHandle_t *const pxCreatedTask) = 0;

    virtual void task_delete(TaskHandle_t task_handle) = 0;

    virtual void set_task_to_notify(TaskHandle_t task_handle) = 0; // TODO: verify if will be necessary on new tests
};