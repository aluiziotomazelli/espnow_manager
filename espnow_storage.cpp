#include "espnow_storage.hpp"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <algorithm>
#include <cinttypes>
#include <cstring>

static const char *TAG           = "EspNowStorage";
static const char *NVS_NAMESPACE = "espnow_store";
static const char *NVS_KEY       = "persist_data";

// RTC memory variable definition
RTC_DATA_ATTR EspNowStorage::PersistentData EspNowStorage::rtc_storage;

EspNowStorage::EspNowStorage()
{
}

EspNowStorage::~EspNowStorage()
{
}

esp_err_t EspNowStorage::init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition invalid, erasing");
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            return err;
        }
        err = nvs_flash_init();
    }
    return err;
}

uint32_t EspNowStorage::calculate_crc(const PersistentData &data)
{
    // Calculate CRC of all fields except the CRC field itself
    size_t length = offsetof(PersistentData, crc);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&data), length);
}

esp_err_t EspNowStorage::load(uint8_t &wifi_channel, std::vector<Peer> &peers)
{
    bool loaded_from_rtc = false;

    // 1. Try RTC
    uint32_t calculated_crc = calculate_crc(rtc_storage);
    if (rtc_storage.magic == ESPNOW_STORAGE_MAGIC &&
        rtc_storage.version == ESPNOW_STORAGE_VERSION &&
        rtc_storage.crc == calculated_crc) {
        ESP_LOGI(TAG, "Loaded data from RTC");
        wifi_channel = rtc_storage.wifi_channel;
        peers.clear();
        for (int i = 0; i < rtc_storage.num_peers; ++i) {
            peers.push_back(rtc_storage.peers[i]);
        }
        loaded_from_rtc = true;
    }
    else {
        ESP_LOGW(TAG,
                 "RTC data invalid or empty. Magic: 0x%08" PRIx32
                 ", CRC: 0x%08" PRIx32 " (Expected: 0x%08" PRIx32 ")",
                 rtc_storage.magic, rtc_storage.crc, calculated_crc);
    }

    if (loaded_from_rtc) {
        return ESP_OK;
    }

    // 2. Try NVS
    esp_err_t err = init_nvs();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS namespace not found: %s", esp_err_to_name(err));
        return err;
    }

    PersistentData nvs_data;
    size_t size = sizeof(PersistentData);
    err         = nvs_get_blob(handle, NVS_KEY, &nvs_data, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS data not found: %s", esp_err_to_name(err));
        return err;
    }

    if (size != sizeof(PersistentData)) {
        ESP_LOGW(TAG, "NVS data has wrong size: %zu (expected: %zu)", size,
                 sizeof(PersistentData));
        return ESP_ERR_INVALID_SIZE;
    }

    calculated_crc = calculate_crc(nvs_data);
    if (nvs_data.magic == ESPNOW_STORAGE_MAGIC &&
        nvs_data.version == ESPNOW_STORAGE_VERSION &&
        nvs_data.crc == calculated_crc) {
        ESP_LOGI(TAG, "Loaded data from NVS");
        wifi_channel = nvs_data.wifi_channel;
        peers.clear();
        for (int i = 0; i < nvs_data.num_peers; ++i) {
            peers.push_back(nvs_data.peers[i]);
        }
        // Update RTC for next time
        memcpy(&rtc_storage, &nvs_data, sizeof(PersistentData));
        return ESP_OK;
    }
    else {
        ESP_LOGE(TAG, "NVS data corrupted");
        return ESP_FAIL;
    }
}

esp_err_t EspNowStorage::save(uint8_t wifi_channel,
                              const std::vector<Peer> &peers,
                              bool force_nvs_commit)
{
    PersistentData data;
    memset(&data, 0, sizeof(PersistentData));
    data.magic        = ESPNOW_STORAGE_MAGIC;
    data.version      = ESPNOW_STORAGE_VERSION;
    data.wifi_channel = wifi_channel;
    data.num_peers    = std::min(peers.size(), MAX_PERSISTENT_PEERS);

    for (size_t i = 0; i < data.num_peers; ++i) {
        data.peers[i] = peers[i];
    }

    data.crc = calculate_crc(data);

    // 1. Check if change is real compared to RTC
    bool is_dirty = (memcmp(&rtc_storage, &data, sizeof(PersistentData)) != 0);

    // 2. Save to RTC
    if (is_dirty) {
        memcpy(&rtc_storage, &data, sizeof(PersistentData));
        ESP_LOGI(TAG, "Saved data to RTC");
    }

    if (!is_dirty && !force_nvs_commit) {
        return ESP_OK;
    }

    // 3. Save to NVS
    esp_err_t err = init_nvs();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, NVS_KEY, &data, sizeof(PersistentData));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved data to NVS");
    }
    else {
        ESP_LOGE(TAG, "Failed to save data to NVS: %s", esp_err_to_name(err));
    }

    return err;
}

#if UNIT_TESTING
void EspNowStorage::test_reset_rtc()
{
    ESP_LOGD(TAG, "Test: Resetting RTC storage");
    memset(&rtc_storage, 0, sizeof(PersistentData));
}

EspNowStorage::PersistentData &EspNowStorage::test_get_rtc()
{
    return rtc_storage;
}

void EspNowStorage::test_inject_rtc(const PersistentData &data)
{
    ESP_LOGD(TAG, "Test: Injecting data into RTC storage");
    memcpy(&rtc_storage, &data, sizeof(PersistentData));
}

uint32_t EspNowStorage::test_calculate_crc(const PersistentData &data)
{
    size_t length = offsetof(PersistentData, crc);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&data), length);
}
#endif