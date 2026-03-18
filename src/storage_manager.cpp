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
    std::unique_ptr<IPersistenceBackend> rtc_backend,
    std::unique_ptr<IPersistenceBackend> nvs_backend)
    : rtc_backend_(std::move(rtc_backend))
    , nvs_backend_(std::move(nvs_backend))
{
}

StorageManager::~StorageManager() {}

uint32_t StorageManager::calculate_crc(const PersistentData &data)
{
    size_t length = offsetof(PersistentData, crc);
    return esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&data), length);
}

esp_err_t StorageManager::validate_and_unpack_data(
    PersistentData &data,
    uint8_t &wifi_channel,
    etl::ivector<PersistentPeer> &peers)

{
    // Validade MAGIC, VERSION and CRC
    if (data.magic != PersistentData::MAGIC) {
        ESP_LOGW(TAG, "Magic mismatch: 0x%08X", data.magic);
        return ESP_ERR_INVALID_STATE;
    }
    if (data.version != PersistentData::VERSION) {
        ESP_LOGW(TAG, "Version mismatch: %d", data.version);
        return ESP_ERR_INVALID_VERSION;
    }
    if (data.crc != calculate_crc(data)) {
        ESP_LOGW(TAG, "CRC mismatch");
        return ESP_ERR_INVALID_CRC;
    }

    // Unpack data
    wifi_channel = data.wifi_channel;
    peers.clear();

    const size_t peers_to_copy = std::min(data.num_peers, (uint8_t)peers.capacity());
    for (size_t i = 0; i < peers_to_copy; ++i) {
        peers.push_back(data.peers[i]);
    }

    return ESP_OK;
}

esp_err_t StorageManager::load(uint8_t &wifi_channel, etl::ivector<PersistentPeer> &peers)
{
    PersistentData data = {};
    esp_err_t ret = ESP_FAIL;

    // 1. Try RTC
    ret = rtc_backend_->load(&data, sizeof(PersistentData));
    if (ret == ESP_OK) {
        ret = validate_and_unpack_data(data, wifi_channel, peers);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Loaded data from RTC");
            return ESP_OK;
        }
    }

    // 2. Try NVS
    ret = nvs_backend_->load(&data, sizeof(PersistentData));
    if (ret == ESP_OK) {
        ret = validate_and_unpack_data(data, wifi_channel, peers);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Loaded data from NVS");
            // Sync RTC
            rtc_backend_->save(&data, sizeof(PersistentData));
            return ESP_OK;
        }
    }

    return ret;
}

bool StorageManager::is_data_dirty(const PersistentData &new_data)
{
    PersistentData current_rtc;

    // If we can't load from RTC, assume it's dirty to be safe
    if (rtc_backend_->load(&current_rtc, sizeof(PersistentData)) != ESP_OK) {
        return true;
    }

    // Using our safe custom comparison operator. This compares only actual field
    // values instead of the entire raw memory block, preventing struct padding bytes
    // or unused array elements from triggering a false "dirty" state, thus
    // avoiding needless NVS flash writes.
    return (current_rtc != new_data);
}

esp_err_t StorageManager::save(uint8_t wifi_channel, const etl::ivector<PersistentPeer> &peers, bool force_nvs_commit)
{
    PersistentData data;
    memset(&data, 0, sizeof(PersistentData));
    data.magic = PersistentData::MAGIC;
    data.version = PersistentData::VERSION;
    data.wifi_channel = wifi_channel;
    data.num_peers = std::min(peers.size(), (size_t)MAX_PEERS);

    for (size_t i = 0; i < data.num_peers; ++i) {
        data.peers[i] = peers[i];
    }

    // Calculate CRC after all fields are set, not used in
    // the comparison operator, only to save data if is dirty
    data.crc = calculate_crc(data);

    // Check if data is dirty
    bool is_dirty = is_data_dirty(data);

    // If data is not dirty and force_nvs_commit is false, return
    if (!is_dirty && !force_nvs_commit) {
        return ESP_OK;
    }

    // If is dirty, persist data to RTC
    if (is_dirty) {
        rtc_backend_->save(&data, sizeof(PersistentData));
        ESP_LOGI(TAG, "Saved data to RTC");
    }

    // Persist data to NVS
    esp_err_t err = nvs_backend_->save(&data, sizeof(PersistentData));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved data to NVS");
    }
    else {
        ESP_LOGE(TAG, "Failed to save data to NVS: %s", esp_err_to_name(err));
    }

    return err;
}
