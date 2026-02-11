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

// --- Real RTC Backend ---
static RTC_DATA_ATTR PersistentData g_rtc_storage;

class RealRtcBackend : public IPersistenceBackend
{
public:
    esp_err_t load(void *data, size_t size) override
    {
        if (size > sizeof(PersistentData)) return ESP_ERR_INVALID_SIZE;
        memcpy(data, &g_rtc_storage, size);
        return ESP_OK;
    }

    esp_err_t save(const void *data, size_t size) override
    {
        if (size > sizeof(PersistentData)) return ESP_ERR_INVALID_SIZE;
        memcpy(&g_rtc_storage, data, size);
        return ESP_OK;
    }
};

// --- Real NVS Backend ---
class RealNvsBackend : public IPersistenceBackend
{
public:
    esp_err_t load(void *data, size_t size) override
    {
        esp_err_t err = init_nvs();
        if (err != ESP_OK) return err;

        nvs_handle_t handle;
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) return err;

        size_t actual_size = size;
        err = nvs_get_blob(handle, NVS_KEY, data, &actual_size);
        nvs_close(handle);

        if (err != ESP_OK) return err;
        if (actual_size != size) return ESP_ERR_INVALID_SIZE;

        return ESP_OK;
    }

    esp_err_t save(const void *data, size_t size) override
    {
        esp_err_t err = init_nvs();
        if (err != ESP_OK) return err;

        nvs_handle_t handle;
        err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err != ESP_OK) return err;

        err = nvs_set_blob(handle, NVS_KEY, data, size);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        nvs_close(handle);
        return err;
    }

private:
    esp_err_t init_nvs()
    {
        static bool nvs_initialized = false;
        if (nvs_initialized) return ESP_OK;

        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        if (err == ESP_OK) nvs_initialized = true;
        return err;
    }
};

// --- EspNowStorage Implementation ---

EspNowStorage::EspNowStorage(std::unique_ptr<IPersistenceBackend> rtc_backend,
                             std::unique_ptr<IPersistenceBackend> nvs_backend)
{
    if (rtc_backend) rtc_backend_ = std::move(rtc_backend);
    else rtc_backend_ = std::make_unique<RealRtcBackend>();

    if (nvs_backend) nvs_backend_ = std::move(nvs_backend);
    else nvs_backend_ = std::make_unique<RealNvsBackend>();
}

EspNowStorage::~EspNowStorage()
{
}

uint32_t EspNowStorage::calculate_crc(const PersistentData &data)
{
    size_t length = offsetof(PersistentData, crc);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&data), length);
}

esp_err_t EspNowStorage::load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers)
{
    PersistentData data;

    // 1. Try RTC
    if (rtc_backend_->load(&data, sizeof(PersistentData)) == ESP_OK) {
        uint32_t calculated_crc = calculate_crc(data);
        if (data.magic == PersistentData::MAGIC &&
            data.version == PersistentData::VERSION &&
            data.crc == calculated_crc) {
            ESP_LOGI(TAG, "Loaded data from RTC");
            wifi_channel = data.wifi_channel;
            peers.clear();
            for (int i = 0; i < data.num_peers; ++i) {
                peers.push_back(data.peers[i]);
            }
            return ESP_OK;
        }
    }

    // 2. Try NVS
    if (nvs_backend_->load(&data, sizeof(PersistentData)) == ESP_OK) {
        uint32_t calculated_crc = calculate_crc(data);
        if (data.magic == PersistentData::MAGIC &&
            data.version == PersistentData::VERSION &&
            data.crc == calculated_crc) {
            ESP_LOGI(TAG, "Loaded data from NVS");
            wifi_channel = data.wifi_channel;
            peers.clear();
            for (int i = 0; i < data.num_peers; ++i) {
                peers.push_back(data.peers[i]);
            }
            // Sync RTC
            rtc_backend_->save(&data, sizeof(PersistentData));
            return ESP_OK;
        }
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t EspNowStorage::save(uint8_t wifi_channel,
                              const std::vector<PersistentPeer> &peers,
                              bool force_nvs_commit)
{
    PersistentData data;
    memset(&data, 0, sizeof(PersistentData));
    data.magic        = PersistentData::MAGIC;
    data.version      = PersistentData::VERSION;
    data.wifi_channel = wifi_channel;
    data.num_peers    = std::min(peers.size(), PersistentData::MAX_PERSISTENT_PEERS);

    for (size_t i = 0; i < data.num_peers; ++i) {
        data.peers[i] = peers[i];
    }

    data.crc = calculate_crc(data);

    // Get current RTC data to check if dirty
    PersistentData current_rtc;
    bool is_dirty = true;
    if (rtc_backend_->load(&current_rtc, sizeof(PersistentData)) == ESP_OK) {
        is_dirty = (memcmp(&current_rtc, &data, sizeof(PersistentData)) != 0);
    }

    if (is_dirty) {
        rtc_backend_->save(&data, sizeof(PersistentData));
        ESP_LOGI(TAG, "Saved data to RTC");
    }

    if (!is_dirty && !force_nvs_commit) {
        return ESP_OK;
    }

    // Save to NVS
    esp_err_t err = nvs_backend_->save(&data, sizeof(PersistentData));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved data to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to save data to NVS: %s", esp_err_to_name(err));
    }

    return err;
}
