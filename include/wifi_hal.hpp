#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "i_wifi_hal.hpp"

class WiFiHAL : public IWiFiHAL
{
public:
    WiFiHAL() = default;

    esp_err_t wifi_set_channel(uint8_t channel) override
    {
        return esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    };
    esp_err_t wifi_get_channel(uint8_t *channel) override { return esp_wifi_get_channel(channel, nullptr); };
    esp_err_t wifi_get_mode(wifi_mode_t *mode) override { return esp_wifi_get_mode(mode); };
    esp_err_t wifi_set_channel(uint8_t primary, wifi_second_chan_t second) override
    {
        return esp_wifi_set_channel(primary, second);
    };
    esp_err_t wifi_get_channel(uint8_t *primary, wifi_second_chan_t *second) override
    {
        return esp_wifi_get_channel(primary, second);
    };

    esp_err_t hal_esp_now_init() override { return esp_now_init(); };
    esp_err_t hal_esp_now_deinit() override { return esp_now_deinit(); };
    esp_err_t hal_espnow_register_recv_cb(esp_now_recv_cb_t cb) override { return esp_now_register_recv_cb(cb); }
    esp_err_t hal_espnow_register_send_cb(esp_now_send_cb_t cb) override { return esp_now_register_send_cb(cb); }
    esp_err_t hal_esp_now_mod_peer(const esp_now_peer_info_t *peer) override { return esp_now_mod_peer(peer); };
    esp_err_t hal_esp_now_add_peer(const esp_now_peer_info_t *peer) override { return esp_now_add_peer(peer); };
    esp_err_t hal_esp_now_del_peer(const uint8_t *peer_addr) override { return esp_now_del_peer(peer_addr); };
    esp_err_t hal_esp_now_send(const uint8_t *mac, const uint8_t *data, size_t len) override
    {
        return esp_now_send(mac, data, len);
    };
};
