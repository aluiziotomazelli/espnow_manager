// src/storage_manager.cpp
#include "esp_log.h"
#include "esp_rom_crc.h"

#include "storage_manager.hpp"

namespace espnow {

static const char* TAG = "StorageManager";

StorageManager::StorageManager(
    std::unique_ptr<IPersistenceBackend> rtc_peers,
    std::unique_ptr<IPersistenceBackend> rtc_channel,
    std::unique_ptr<IPersistenceBackend> rtc_stats,
    std::unique_ptr<IPersistenceBackend> nvs_peers,
    std::unique_ptr<IPersistenceBackend> nvs_channel,
    std::unique_ptr<IPersistenceBackend> nvs_stats)
    : rtc_peers_backend_(std::move(rtc_peers))
    , rtc_channel_backend_(std::move(rtc_channel))
    , rtc_stats_backend_(std::move(rtc_stats))
    , nvs_peers_backend_(std::move(nvs_peers))
    , nvs_channel_backend_(std::move(nvs_channel))
    , nvs_stats_backend_(std::move(nvs_stats))
{
}

StorageManager::~StorageManager() {}

template <typename T> uint32_t StorageManager::calculate_crc(const T& data)
{
    static_assert(std::is_standard_layout_v<T>, "T must be standard layout for offset");
    static_assert(offsetof(T, crc) != 0, "T must have a crc field");
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t*>(&data), offsetof(T, crc));
}

esp_err_t StorageManager::load_channel(uint8_t& wifi_channel)
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

esp_err_t StorageManager::load_peers(etl::ivector<PersistentPeer>& peers)
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

esp_err_t StorageManager::store_peers(const etl::ivector<PersistentPeer>& peers, bool force_nvs_commit)
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

esp_err_t StorageManager::load_stats(etl::ivector<PeerStatisticsPersist>& stats)
{
    PersistentStats stats_data = {};
    esp_err_t ret = load_raw_stats(stats_data);
    if (ret != ESP_OK) {
        return ret;
    }

    stats.clear();
    const uint8_t count = std::min(stats_data.num_stats, (uint8_t)stats.capacity());
    for (uint8_t i = 0; i < count; ++i) {
        stats.push_back(stats_data.stats[i]);
    }
    return ESP_OK;
}

esp_err_t StorageManager::store_stats(const etl::ivector<PeerStatisticsPersist>& stats)
{
    PersistentStats data = {};
    data.magic = PersistentStats::MAGIC;
    data.version = PersistentStats::VERSION;
    data.num_stats = std::min(stats.size(), (size_t)MAX_PEERS);

    for (size_t i = 0; i < data.num_stats; ++i) {
        data.stats[i] = stats[i];
    }

    // Check if data is dirty
    bool is_dirty = is_data_dirty(data);

    // Calculate CRC to save if data is dirty
    data.crc = calculate_crc(data);

    // If data is not dirty, return OK
    if (!is_dirty) {
        return ESP_OK;
    }

    // If is dirty, persist data to RTC and NVS
    rtc_stats_backend_->save(&data, sizeof(data));
    esp_err_t err = nvs_stats_backend_->save(&data, sizeof(data));

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved statistics to Storage");
    }
    else {
        ESP_LOGE(TAG, "Failed to save statistics to NVS: %s", esp_err_to_name(err));
    }

    return err;
}

// ================================================================
// Private helpers
// ================================================================

esp_err_t StorageManager::load_raw_peers(PersistentPeers& out)
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
            // Sync RTC with NVS
            rtc_peers_backend_->save(&out, sizeof(PersistentPeers));
            ESP_LOGD(TAG, "Loaded data from NVS");
            return ESP_OK;
        }
    }
    return ret; // nothing valid found
}

esp_err_t StorageManager::validate_peers_data(const PersistentPeers& data)
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

bool StorageManager::is_data_dirty(const PersistentPeers& new_peers)
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

esp_err_t StorageManager::load_raw_channel(PersistentChannel& out)
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
            // Sync RTC with NVS
            rtc_channel_backend_->save(&out, sizeof(PersistentChannel));
            ESP_LOGD(TAG, "Loaded data from NVS");
            return ESP_OK;
        }
    }
    return ret; // nothing valid found
}

esp_err_t StorageManager::validate_channel_data(const PersistentChannel& channel)
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

bool StorageManager::is_data_dirty(const PersistentChannel& new_channel)
{
    PersistentChannel current_rtc;

    // If we can't load from RTC, assume it's dirty to be safe
    if (rtc_channel_backend_->load(&current_rtc, sizeof(PersistentChannel)) != ESP_OK) {
        return true;
    }

    return (current_rtc != new_channel);
}

esp_err_t StorageManager::load_raw_stats(PersistentStats& out)
{
    esp_err_t ret;
    // 1. Try RTC first (fast, survives deep-sleep)
    ret = rtc_stats_backend_->load(&out, sizeof(PersistentStats));
    if (ret == ESP_OK) {
        ret = validate_stats_data(out);
        if (ret == ESP_OK) {
            ESP_LOGD(TAG, "Loaded stats from RTC");
            return ESP_OK;
        }
    }
    // 2. Fall back to NVS
    ret = nvs_stats_backend_->load(&out, sizeof(PersistentStats));
    if (ret == ESP_OK) {
        ret = validate_stats_data(out);
        if (ret == ESP_OK) {
            // Sync RTC with NVS
            rtc_stats_backend_->save(&out, sizeof(PersistentStats));
            ESP_LOGD(TAG, "Loaded stats from NVS");
            return ESP_OK;
        }
    }
    return ret; // nothing valid found
}

esp_err_t StorageManager::validate_stats_data(const PersistentStats& data)
{
    // Validade MAGIC, VERSION and CRC
    if (data.magic != PersistentStats::MAGIC) {
        ESP_LOGW(TAG, "Stats magic mismatch: 0x%08X", (unsigned int)data.magic);
        return ESP_ERR_INVALID_STATE;
    }
    if (data.version != PersistentStats::VERSION) {
        ESP_LOGW(TAG, "Stats version mismatch: %d", (int)data.version);
        return ESP_ERR_INVALID_VERSION;
    }
    if (data.crc != calculate_crc(data)) {
        ESP_LOGW(TAG, "Stats CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

bool StorageManager::is_data_dirty(const PersistentStats& new_stats)
{
    PersistentStats current_rtc;

    // If we can't load from RTC, assume it's dirty to be safe
    if (rtc_stats_backend_->load(&current_rtc, sizeof(PersistentStats)) != ESP_OK) {
        return true;
    }

    return (current_rtc != new_stats);
}

} // namespace espnow
