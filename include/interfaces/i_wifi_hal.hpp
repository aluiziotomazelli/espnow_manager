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
};