// src/espnow_driver.cpp

#include "esp_log.h"

#include "espnow_driver.hpp"

namespace espnow {

const char* TAG = "EspNowDriver";

EspNowDriver::EspNowDriver(IWiFiHAL& wifi_hal, IEspNowHAL& espnow_hal)
    : wifi_hal_(wifi_hal)
    , espnow_hal_(espnow_hal)
{
}

esp_err_t EspNowDriver::init(const EspNowConfig& config, esp_now_recv_cb_t recv_cb, esp_now_send_cb_t send_cb)
{
    esp_err_t err;

    wifi_mode_t mode;
    err = wifi_hal_.wifi_get_mode(&mode);
    if (err != ESP_OK) {
        return init_fail(err, "Failed to get WiFi mode");
    }
    if (mode == WIFI_MODE_NULL) {
        return init_fail(ESP_ERR_INVALID_STATE, "Mode is not supported");
    }

    err = espnow_hal_.hal_esp_now_init();
    if (err != ESP_OK) {
        return init_fail(err, "Failed to initialize ESP-NOW");
    }

    err = espnow_hal_.hal_espnow_register_recv_cb(recv_cb);
    if (err != ESP_OK) {
        return init_fail(err, "Failed to register ESP-NOW receive callback");
    }

    err = espnow_hal_.hal_espnow_register_send_cb(send_cb);
    if (err != ESP_OK) {
        return init_fail(err, "Failed to register ESP-NOW send callback");
    }

    err = wifi_hal_.wifi_set_channel(config.wifi_channel, WIFI_SECOND_CHAN_NONE);
    if (err != ESP_OK) {
        return init_fail(err, "Failed to set WiFi channel");
    }

    err = add_broadcast_peer();
    if (err != ESP_OK) {
        return init_fail(err, "Failed to add broadcast peer");
    }

    return ESP_OK;
}

esp_err_t EspNowDriver::deinit()
{
    return espnow_hal_.hal_esp_now_deinit();
}

// ===============================================================
// Private methods
// ===============================================================

// Add broadcast peer to ESP-NOW
esp_err_t EspNowDriver::add_broadcast_peer()
{
    esp_now_peer_info_t broadcast_peer = {};
    memcpy(broadcast_peer.peer_addr, BROADCAST_MAC, 6);
    broadcast_peer.channel = 0;
    broadcast_peer.ifidx = WIFI_IF_STA;
    broadcast_peer.encrypt = false;
    return espnow_hal_.hal_esp_now_add_peer(&broadcast_peer);
}

// Helper to log error and deinit
esp_err_t EspNowDriver::init_fail(esp_err_t ret, const char* step)
{
    ESP_LOGE(TAG, "%s failed: %s", step, esp_err_to_name(ret));
    deinit();
    return ret;
}

} // namespace espnow