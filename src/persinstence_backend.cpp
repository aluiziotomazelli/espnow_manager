// src/persistence_backends.cpp

#include "esp_log.h"
// #include "esp_rom_crc.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "persistence_backends.hpp"

static const char *TAG = "PersistenceBackend";
static const char *NVS_NAMESPACE = "espnow_store";
static const char *NVS_KEY = "persist_data";

// --- RTC Backend ---
static RTC_DATA_ATTR PersistentData g_rtc_storage;

RtcBackend::RtcBackend(PersistentData *storage_ptr)
    : storage_(storage_ptr ? storage_ptr : &g_rtc_storage)
{
}

esp_err_t RtcBackend::load(void *data, size_t size)
{
    if (size > sizeof(PersistentData))
        return ESP_ERR_INVALID_SIZE;
    memcpy(data, storage_, size);
    return ESP_OK;
}

esp_err_t RtcBackend::save(const void *data, size_t size)
{
    if (size > sizeof(PersistentData))
        return ESP_ERR_INVALID_SIZE;
    memcpy(storage_, data, size);
    return ESP_OK;
}

// --- NVS Backend ---

esp_err_t NvsBackend::load(void *data, size_t size)
{
    esp_err_t err = init_nvs();
    if (err != ESP_OK)
        return err;

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK)
        return err;

    size_t actual_size = size;
    err = nvs_get_blob(handle, NVS_KEY, data, &actual_size);
    nvs_close(handle);

    if (err != ESP_OK)
        return err;
    if (actual_size != size)
        return ESP_ERR_INVALID_SIZE;

    return ESP_OK;
}

esp_err_t NvsBackend::save(const void *data, size_t size)
{
    esp_err_t err = init_nvs();
    if (err != ESP_OK)
        return err;

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;

    err = nvs_set_blob(handle, NVS_KEY, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save data to NVS: 0x%x", err);
    }

    return err;
}

esp_err_t NvsBackend::init_nvs()
{
    static bool nvs_initialized = false;
    if (nvs_initialized)
        return ESP_OK;

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err == ESP_OK)
        nvs_initialized = true;
    return err;
}