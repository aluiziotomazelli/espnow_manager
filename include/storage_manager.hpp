#pragma once

#include <cstdint>
#include <cstring>
#include <memory>
#include <tuple> // Required for std::tie

#include "etl/vector.h"

#include "esp_err.h"

#include "i_persistence_backend.hpp"
#include "i_storage_manager.hpp"

// Forward declarations for PersistentPeer and MAX_PEERS if they are defined elsewhere and needed.
// Assuming PersistentPeer is defined and MAX_PEERS is available in the scope.
// If MAX_PEERS is not globally available here, it might need to be included from espnow_types.hpp or similar.
// For now, assuming MAX_PEERS is visible in this translation unit.

/**
 * @brief Internal structure for persistent data.
 * This structure is serialized and stored in RTC RAM and NVS.
 */
struct PersistentData
{
    // Schema identifiers (constants, not user data for dirty check)
    static constexpr uint32_t MAGIC = 0x4553504E;
    static constexpr uint32_t VERSION = 1;

    // Actual data fields
    uint32_t magic;
    uint32_t version;
    uint8_t wifi_channel;
    uint8_t num_peers;
    PersistentPeer peers[MAX_PEERS]; // Array of peers, size determined by MAX_PEERS
    uint32_t crc;                    // CRC for data integrity
};

/**
 * @brief Class to handle persistence of EspNowManager component data in RTC memory and NVS.
 */
class StorageManager : public IStorageManager
{
public:
    StorageManager(
        std::unique_ptr<IPersistenceBackend> rtc_backend = nullptr,
        std::unique_ptr<IPersistenceBackend> nvs_backend = nullptr);

    ~StorageManager();

    /**
     * @brief Loads data from RTC or NVS.
     *
     * @param wifi_channel Output for the loaded wifi channel.
     * @param peers Output for the loaded peer list.
     * @return ESP_OK if loaded successfully, error otherwise.
     */
    esp_err_t load(uint8_t &wifi_channel, etl::ivector<PersistentPeer> &peers) override;

    /**
     * @brief Saves data to RTC and NVS.
     *
     * @param wifi_channel Current wifi channel.
     * @param peers Current peer list.
     * @param force_nvs_commit If true, forces a save to NVS even if data seems
     * unchanged.
     * @return ESP_OK if saved successfully, error otherwise.
     */
    esp_err_t
    save(uint8_t wifi_channel, const etl::ivector<PersistentPeer> &peers, bool force_nvs_commit = true) override;

    static uint32_t calculate_crc(const PersistentData &data);

private:
    std::unique_ptr<IPersistenceBackend> rtc_backend_;
    std::unique_ptr<IPersistenceBackend> nvs_backend_;
};
