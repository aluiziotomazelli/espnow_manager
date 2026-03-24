// #include <algorithm>
// #include <cinttypes>
// #include <cstring>

// #include "esp_attr.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
// #include "nvs.h"
// #include "nvs_flash.h"

#include "storage_manager.hpp"
// #include "persistence_backend.hpp"

static const char *TAG = "StorageManager";

StorageManager::StorageManager(
    std::unique_ptr<IPersistenceBackend> rtc_peers,
    std::unique_ptr<IPersistenceBackend> nvs_peers,
    std::unique_ptr<IPersistenceBackend> rtc_channel,
    std::unique_ptr<IPersistenceBackend> nvs_channel)
    : rtc_peers_backend_(std::move(rtc_peers))
    , nvs_peers_backend_(std::move(nvs_peers))
    , rtc_channel_backend_(std::move(rtc_channel))
    , nvs_channel_backend_(std::move(nvs_channel))
{
}

StorageManager::~StorageManager() {}

template <typename T> uint32_t StorageManager::calculate_crc(const T &data)
{
    static_assert(std::is_standard_layout_v<T>, "T must be standard layout for offset");
    static_assert(offsetof(T, crc) != 0, "T must have a crc field");
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&data), offsetof(T, crc));
}

esp_err_t StorageManager::load_channel(uint8_t &wifi_channel)
{
    PersistentChannel data = {};

    esp_err_t ret;
    ret = load_raw_channel(data);

    if (ret == ESP_OK) {
        wifi_channel = data.wifi_channel;
    }
    return ret;
}

esp_err_t StorageManager::store_channel(uint8_t channel)
{
    PersistentChannel data = {};
    data.magic = PersistentChannel::MAGIC;
    data.wifi_channel = channel;

    bool is_dirty = is_data_dirty(data);
    if (!is_dirty) {
        return ESP_OK;
    }

    data.crc = calculate_crc(data);
    rtc_channel_backend_->save(&data, sizeof(data));

    esp_err_t ret = nvs_channel_backend_->save(&data, sizeof(data));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Saved channel to Storage");
    }
    else {
        ESP_LOGE(TAG, "Failed to save channel to NVS: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t StorageManager::load_peers(etl::ivector<PersistentPeer> &peers)
{
    PersistentPeers data = {};

    esp_err_t ret;
    ret = load_raw_peers(data);

    if (ret == ESP_OK) {
        // Ensure vector is empty before populating
        peers.clear();
        // Populate vector with peers from data
        const size_t peers_to_copy = std::min(data.num_peers, (uint8_t)peers.capacity());
        for (size_t i = 0; i < peers_to_copy; ++i) {
            peers.push_back(data.peers[i]);
        }
    }
    return ret;
}

esp_err_t StorageManager::store_peers(const etl::ivector<PersistentPeer> &peers, bool force_nvs_commit)
{
    PersistentPeers data = {};
    data.magic = PersistentPeers::MAGIC;
    data.version = PersistentPeers::VERSION;
    data.num_peers = std::min(peers.size(), (size_t)MAX_PEERS);

    for (size_t i = 0; i < data.num_peers; ++i) {
        data.peers[i] = peers[i];
    }

    // Check if data is dirty
    bool is_dirty = is_data_dirty(data);

    // Calculate CRC to save if data is dirty
    data.crc = calculate_crc(data);

    // If data is not dirty and force_nvs_commit is false, return
    if (!is_dirty && !force_nvs_commit) {
        return ESP_OK;
    }

    // If is dirty, persist data to RTC
    if (is_dirty) {
        rtc_peers_backend_->save(&data, sizeof(data));
        ESP_LOGI(TAG, "Saved data to RTC");
    }

    // Persist data to NVS
    esp_err_t err = nvs_peers_backend_->save(&data, sizeof(data));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved data to NVS");
    }
    else {
        ESP_LOGE(TAG, "Failed to save data to NVS: %s", esp_err_to_name(err));
    }

    return err;
}

// ================================================================
// Private helpers
// ================================================================

esp_err_t StorageManager::load_raw_peers(PersistentPeers &out)
{
    esp_err_t ret;
    // 1. Try RTC first (fast, survives deep-sleep)
    ret = rtc_peers_backend_->load(&out, sizeof(PersistentPeers));
    if (ret == ESP_OK) {
        ret = validate_peers_data(out);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Loaded data from RTC");
            return ESP_OK;
        }
    }
    // 2. Fall back to NVS
    ret = nvs_peers_backend_->load(&out, sizeof(PersistentPeers));
    if (ret == ESP_OK) {
        ret = validate_peers_data(out);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Loaded data from NVS");
            return ESP_OK;
        }
    }
    return ret; // nothing valid found
}

esp_err_t StorageManager::validate_peers_data(const PersistentPeers &data)
{
    // Validade MAGIC, VERSION and CRC
    if (data.magic != PersistentPeers::MAGIC) {
        ESP_LOGW(TAG, "Magic mismatch: 0x%08X", data.magic);
        return ESP_ERR_INVALID_STATE;
    }
    if (data.version != PersistentPeers::VERSION) {
        ESP_LOGW(TAG, "Version mismatch: %d", data.version);
        return ESP_ERR_INVALID_VERSION;
    }
    if (data.crc != calculate_crc(data)) {
        ESP_LOGW(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

bool StorageManager::is_data_dirty(const PersistentPeers &new_peers)
{
    PersistentPeers current_rtc;

    // If we can't load from RTC, assume it's dirty to be safe
    if (rtc_peers_backend_->load(&current_rtc, sizeof(PersistentPeers)) != ESP_OK) {
        return true;
    }

    // Using our safe custom comparison operator. This compares only actual field
    // values instead of the entire raw memory block, preventing struct padding bytes
    // or unused array elements from triggering a false "dirty" state, thus
    // avoiding needless NVS flash writes.
    return (current_rtc != new_peers);
}

esp_err_t StorageManager::load_raw_channel(PersistentChannel &out)
{
    esp_err_t ret;
    // 1. Try RTC first (fast, survives deep-sleep)
    ret = rtc_channel_backend_->load(&out, sizeof(PersistentChannel));
    if (ret == ESP_OK) {
        ret = validate_channel_data(out);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Loaded data from RTC");
            return ESP_OK;
        }
    }
    // 2. Fall back to NVS
    ret = nvs_channel_backend_->load(&out, sizeof(PersistentChannel));
    if (ret == ESP_OK) {
        ret = validate_channel_data(out);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Loaded data from NVS");
            return ESP_OK;
        }
    }
    return ret; // nothing valid found
}

esp_err_t StorageManager::validate_channel_data(const PersistentChannel &channel)
{
    // Validade MAGIC and CRC
    if (channel.magic != PersistentChannel::MAGIC) {
        ESP_LOGW(TAG, "Magic mismatch: 0x%08X", channel.magic);
        return ESP_ERR_INVALID_STATE;
    }
    if (channel.crc != calculate_crc(channel)) {
        ESP_LOGW(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

bool StorageManager::is_data_dirty(const PersistentChannel &new_channel)
{
    PersistentChannel current_rtc;

    // If we can't load from RTC, assume it's dirty to be safe
    if (rtc_channel_backend_->load(&current_rtc, sizeof(PersistentChannel)) != ESP_OK) {
        return true;
    }

    return (current_rtc != new_channel);
}
