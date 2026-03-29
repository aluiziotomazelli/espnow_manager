// host_test/common/mock_hal_espnow.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_hal_espnow.hpp"

class MockEspNowHAL : public IEspNowHAL
{
public:
    MOCK_METHOD(esp_err_t, hal_esp_now_init, (), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_deinit, (), (override));
    MOCK_METHOD(esp_err_t, hal_espnow_register_recv_cb, (esp_now_recv_cb_t cb), (override));
    MOCK_METHOD(esp_err_t, hal_espnow_register_send_cb, (esp_now_send_cb_t cb), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_add_peer, (const esp_now_peer_info_t *peer), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_mod_peer, (const esp_now_peer_info_t *peer), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_del_peer, (const uint8_t *peer_addr), (override));
    MOCK_METHOD(esp_err_t, hal_esp_now_send, (const uint8_t *mac, const uint8_t *data, size_t len), (override));
};
