// include/internface/i_hal_wifi.hpp
#pragma once

#include "esp_err.h"
#include "nvs.h"

/**
 * @interface INvsHAL
 * @brief Hardware Abstraction Layer for NVS drivers (internal)
 * @internal
 */
class INvsHAL
{
public:
    virtual ~INvsHAL() = default;

    virtual esp_err_t hal_nvs_flash_init() = 0;
    virtual esp_err_t hal_nvs_flash_erase() = 0;
    virtual esp_err_t hal_nvs_erase_all(nvs_handle_t handle) = 0;
    virtual esp_err_t hal_nvs_open(const char *namespace_name, nvs_open_mode_t open_mode, nvs_handle_t *out_handle) = 0;
    virtual void hal_nvs_close(nvs_handle_t handle) = 0;
    virtual esp_err_t hal_nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t data_size) = 0;
    virtual esp_err_t hal_nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *data_size) = 0;
    virtual esp_err_t hal_nvs_commit(nvs_handle_t handle) = 0;
};