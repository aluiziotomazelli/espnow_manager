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
 * @brief Internal structure for persistent peers data
 * This structure is serialized and stored in RTC RAM and NVS.
 */
struct PersistentPeers
{
    // Schema identifiers (constants, not user data for dirty check)
    static constexpr uint32_t MAGIC = 0x4553504E;
    static constexpr uint32_t VERSION = 1;

    // Actual data fields
    uint32_t magic;
    uint32_t version;
    uint8_t num_peers;
    PersistentPeer peers[MAX_PEERS]; // Array of peers, size determined by MAX_PEERS
    uint32_t crc;                    // CRC for data integrity

    /**
     * @brief Custom equality operator for dirty-state validation.
     *
     * We avoid a simple memcmp(this, &other, sizeof(PersistentData)) because:
     * 1. Struct padding might contain uninitialized junk from memory assignments.
     * 2. We don't want to evaluate the unused elements in the `peers` array
     *    (indexes from num_peers up to MAX_PEERS).
     * 3. magic, version, and crc aren't mutable logical user data.
     *
     * This guarantees we only write to NVS when actual user state has changed.
     */
    bool operator==(const PersistentPeers& other) const
    {
        if (std::tie(num_peers) != std::tie(other.num_peers)) {
            return false;
        }

        // Only evaluate the active peers, ignoring trailing unused entries in the array.
        for (uint8_t i = 0; i < num_peers; ++i) {
            if (peers[i] != other.peers[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator!=(const PersistentPeers& other) const { return !(*this == other); }
};

/**
 * @brief Internal structure for persistent channel data
 * This structure is serialized and stored in RTC RAM and NVS.
 */
struct PersistentChannel
{
    // Magic number to identify the structure
    static constexpr uint32_t MAGIC = 0x4348414E; // "CHAN"

    // Actual data fields
    uint32_t magic;
    uint8_t wifi_channel;
    uint32_t crc; // CRC for data integrity

    bool operator==(const PersistentChannel& other) const
    {
        if (wifi_channel != other.wifi_channel) {
            return false;
        }

        return true;
    }

    bool operator!=(const PersistentChannel& other) const { return !(*this == other); }
};

/**
 * @brief Internal structure for persistent statistics data
 */
struct PersistentStats
{
    static constexpr uint32_t MAGIC = 0x53544154; // "STAT"
    static constexpr uint32_t VERSION = 1;

    uint32_t magic;
    uint32_t version;
    uint8_t num_stats;
    PeerStatisticsPersist stats[MAX_PEERS];
    uint32_t crc;

    bool operator==(const PersistentStats& other) const
    {
        if (num_stats != other.num_stats)
            return false;
        for (uint8_t i = 0; i < num_stats; ++i) {
            if (stats[i].node_id != other.stats[i].node_id || stats[i].rssi_avg != other.stats[i].rssi_avg ||
                stats[i].packets_rx != other.stats[i].packets_rx || stats[i].packets_tx != other.stats[i].packets_tx ||
                stats[i].packets_lost != other.stats[i].packets_lost ||
                stats[i].rtt_avg_ms != other.stats[i].rtt_avg_ms) {
                return false;
            }
        }
        return true;
    }
    bool operator!=(const PersistentStats& other) const { return !(*this == other); }
};

/**
 * @brief Class to handle persistence of EspNowManager component data in RTC memory and NVS.
 */
class StorageManager : public IStorageManager
{
public:
    StorageManager(
        std::unique_ptr<IPersistenceBackend> rtc_peers,
        std::unique_ptr<IPersistenceBackend> rtc_channel,
        std::unique_ptr<IPersistenceBackend> rtc_stats,
        std::unique_ptr<IPersistenceBackend> nvs_peers,
        std::unique_ptr<IPersistenceBackend> nvs_channel,
        std::unique_ptr<IPersistenceBackend> nvs_stats);

    ~StorageManager() override;

    /** @copydoc IStorageManager::load_channel */
    esp_err_t load_channel(uint8_t& channel) override;

    /** @copydoc IStorageManager::store_channel */
    esp_err_t store_channel(uint8_t channel) override;

    /** @copydoc IStorageManager::load_peers */
    esp_err_t load_peers(etl::ivector<PersistentPeer>& peers) override;

    /** @copydoc IStorageManager::store_peers */
    esp_err_t store_peers(const etl::ivector<PersistentPeer>& peers, bool force_nvs_commit = true) override;

    /** @copydoc IStorageManager::load_stats */
    esp_err_t load_stats(etl::ivector<PeerStatisticsPersist>& stats) override;

    /** @copydoc IStorageManager::store_stats */
    esp_err_t store_stats(const PeerStatisticsPersist& stats) override;

    /**
     * @brief Calculates the CRC of the given data.
     * @tparam T The type of the data to calculate the CRC of.
     * @param data The data to calculate the CRC of.
     * @return The CRC of the given data.
     */
    template <typename T> static uint32_t calculate_crc(const T& data);

private:
    std::unique_ptr<IPersistenceBackend> rtc_peers_backend_;
    std::unique_ptr<IPersistenceBackend> rtc_channel_backend_;
    std::unique_ptr<IPersistenceBackend> rtc_stats_backend_;
    std::unique_ptr<IPersistenceBackend> nvs_peers_backend_;
    std::unique_ptr<IPersistenceBackend> nvs_channel_backend_;
    std::unique_ptr<IPersistenceBackend> nvs_stats_backend_;

    esp_err_t load_raw_peers(PersistentPeers& out_peers);
    esp_err_t validate_peers_data(const PersistentPeers& peers);
    bool is_data_dirty(const PersistentPeers& new_peers);

    esp_err_t load_raw_channel(PersistentChannel& out_channel);
    esp_err_t validate_channel_data(const PersistentChannel& channel);
    bool is_data_dirty(const PersistentChannel& new_channel);

    esp_err_t load_raw_stats(PersistentStats& out_stats);
    esp_err_t validate_stats_data(const PersistentStats& stats);
    bool is_data_dirty(const PersistentStats& new_stats);
};
