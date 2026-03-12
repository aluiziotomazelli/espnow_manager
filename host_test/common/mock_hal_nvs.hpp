// host_test/common/mock_wifi_driver_hal.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_hal_wifi.hpp"

class MockNvsHAL : public INvsHAL
{
public:
    MOCK_METHOD(esp_err_t, hal_nvs_flash_init, ());
    MOCK_METHOD(esp_err_t, hal_nvs_flash_erase, ());
    MOCK_METHOD(esp_err_t, hal_nvs_erase_all, (nvs_handle_t handle));
    MOCK_METHOD(
        esp_err_t,
        hal_nvs_open,
        (const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle));
    MOCK_METHOD(void, hal_nvs_close, (nvs_handle_t handle));
    MOCK_METHOD(
        esp_err_t,
        hal_nvs_set_blob,
        (nvs_handle_t handle, const char *key, const void *data, size_t data_size));
    MOCK_METHOD(esp_err_t, hal_nvs_get_blob, (nvs_handle_t handle, const char *key, void *data, size_t *data_size));
    MOCK_METHOD(esp_err_t, hal_nvs_commit, (nvs_handle_t handle));
};
