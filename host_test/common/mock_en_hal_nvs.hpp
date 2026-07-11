// host_test/common/mock_hal_nvs.hpp
#pragma once

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "i_en_hal_nvs.hpp"

namespace espnow {

class MockNvsHAL : public espnow::INvsHAL
{
public:
    MOCK_METHOD(esp_err_t, hal_nvs_flash_init, (), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_flash_erase, (), (override));
    MOCK_METHOD(esp_err_t, hal_nvs_erase_all, (nvs_handle_t handle), (override));
    MOCK_METHOD(
        esp_err_t,
        hal_nvs_open,
        (const char* name, nvs_open_mode_t open_mode, nvs_handle_t* out_handle),
        (override));
    MOCK_METHOD(void, hal_nvs_close, (nvs_handle_t handle), (override));
    MOCK_METHOD(
        esp_err_t,
        hal_nvs_set_blob,
        (nvs_handle_t handle, const char* key, const void* value, size_t length),
        (override));
    MOCK_METHOD(
        esp_err_t,
        hal_nvs_get_blob,
        (nvs_handle_t handle, const char* key, void* out_value, size_t* length),
        (override));
    MOCK_METHOD(esp_err_t, hal_nvs_commit, (nvs_handle_t handle), (override));
};

} // namespace espnow
