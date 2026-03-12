// include/hal_wifi.hpp
#pragma once

#include "esp_err.h"

#include "i_hal_wifi.hpp"
#include "nvs_flash.h"

/**
 * Class NvsHAL
 * @brief Hardware Abstraction Layer for NVS drivers (internal)
 * @internal
 */
class NvsHAL : public INvsHAL
{
public:
    NvsHAL() = default;

    esp_err_t hal_nvs_flash_init() override { return nvs_flash_init(); };
    esp_err_t hal_nvs_flash_erase() override { return nvs_flash_erase(); };
    esp_err_t hal_nvs_erase_all(nvs_handle_t handle) override { return nvs_erase_all(handle); };
    esp_err_t hal_nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) override
    {
        return nvs_open(namespace_name, open_mode, out_handle);
    };
    void hal_nvs_close(nvs_handle_t handle) override { nvs_close(handle); };
    esp_err_t hal_nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t data_size) override
    {
        return nvs_set_blob(handle, key, data, data_size);
    };
    esp_err_t hal_nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *data_size) override
    {
        return nvs_get_blob(handle, key, data, data_size);
    };
    esp_err_t hal_nvs_commit(nvs_handle_t handle) override { return nvs_commit(handle); };
};