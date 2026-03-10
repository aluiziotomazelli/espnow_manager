// #include <algorithm>
// #include <cinttypes>
// #include <cstring>

// #include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
// #include "nvs.h"
// #include "nvs_flash.h"

#include "storage_manager.hpp"
#include "persistence_backends.hpp"

static const char *TAG = "StorageManager";

StorageManager::StorageManager(
    std::unique_ptr<IPersistenceBackend> rtc_backend,
    std::unique_ptr<IPersistenceBackend> nvs_backend)
{
    if (rtc_backend)
        rtc_backend_ = std::move(rtc_backend);
    else
        rtc_backend_ = std::make_unique<RtcBackend>();

    if (nvs_backend)
        nvs_backend_ = std::move(nvs_backend);
    else
        nvs_backend_ = std::make_unique<NvsBackend>();
}

StorageManager::~StorageManager() {}

uint32_t StorageManager::calculate_crc(const PersistentData &data)
{
    size_t length = offsetof(PersistentData, crc);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&data), length);
}

esp_err_t StorageManager::load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers)
{
    PersistentData data;

    // 1. Try RTC
    if (rtc_backend_->load(&data, sizeof(PersistentData)) == ESP_OK) {
        uint32_t calculated_crc = calculate_crc(data);
        if (data.magic == PersistentData::MAGIC && data.version == PersistentData::VERSION &&
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
        if (data.magic == PersistentData::MAGIC && data.version == PersistentData::VERSION &&
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

esp_err_t StorageManager::save(uint8_t wifi_channel, const std::vector<PersistentPeer> &peers, bool force_nvs_commit)
{
    PersistentData data;
    memset(&data, 0, sizeof(PersistentData));
    data.magic = PersistentData::MAGIC;
    data.version = PersistentData::VERSION;
    data.wifi_channel = wifi_channel;
    data.num_peers = std::min(peers.size(), PersistentData::MAX_PERSISTENT_PEERS);

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
    }
    else {
        ESP_LOGE(TAG, "Failed to save data to NVS: %s", esp_err_to_name(err));
    }

    return err;
}
