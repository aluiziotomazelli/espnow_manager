// host_test/common/mock_wifi_driver_hal.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_wifi_hal.hpp"

class MockWiFiHAL : public IWiFiHAL
{
public:
    MOCK_METHOD(esp_err_t, wifi_set_channel, (uint8_t channel), (override));
    MOCK_METHOD(esp_err_t, wifi_get_channel, (uint8_t *channel), (override));
    MOCK_METHOD(esp_err_t, wifi_get_mode, (wifi_mode_t * mode), (override));
    MOCK_METHOD(esp_err_t, wifi_set_channel, (uint8_t primary, wifi_second_chan_t second), (override));
    MOCK_METHOD(esp_err_t, wifi_get_channel, (uint8_t *primary, wifi_second_chan_t *second), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_init, (), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_deinit, (), (override));
    MOCK_METHOD(esp_err_t, hal_espnow_register_recv_cb, (esp_now_recv_cb_t cb), (override));
    MOCK_METHOD(esp_err_t, hal_espnow_register_send_cb, (esp_now_send_cb_t cb), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_add_peer, (const esp_now_peer_info_t *peer), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_mod_peer, (const esp_now_peer_info_t *peer), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_del_peer, (const uint8_t *peer_addr), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_send, (const uint8_t *mac, const uint8_t *data, size_t len), (override));
    MOCK_METHOD(
        BaseType_t,
        hal_task_notify_wait,
        (uint32_t bits_to_clear, uint32_t *notification_value, uint32_t timeout_ms),
        (override));
    MOCK_METHOD(
        BaseType_t,
        task_create,
        (TaskFunction_t pvTaskCode,
         const char *pcName,
         uint32_t usStackDepth,
         void *pvParameters,
         UBaseType_t uxPriority,
         TaskHandle_t *pxCreatedTask),
        (override));
    MOCK_METHOD(void, task_delete, (TaskHandle_t xTaskToDelete), (override));
    MOCK_METHOD(void, set_task_to_notify, (TaskHandle_t task_handle), (override));
};