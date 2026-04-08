// include/interfaces/i_storage_manager.hpp
#pragma once

#include <cstdint>

#include "etl/vector.h"

#include "esp_err.h"

#include "espnow_types.hpp"

/**
 * @interface IStorageManager
 * @brief Higher-level storage management for peers and config.
 * @internal
 */
class IStorageManager
{
public:
    virtual ~IStorageManager() = default;

    /**
     * @brief Loads the WiFi channel from storage.
     * @param channel Output for the loaded WiFi channel.
     * @return ESP_OK if loaded successfully, error otherwise.
     * @internal
     */
    virtual esp_err_t load_channel(uint8_t& channel) = 0;

    /**
     * @brief Stores the WiFi channel to storage.
     * @param channel The WiFi channel to store.
     * @return ESP_OK if stored successfully, error otherwise.
     * @internal
     */
    virtual esp_err_t store_channel(uint8_t channel) = 0;

    /**
     * @brief Loads the peers from storage.
     * @param peers Output for the loaded peers.
     * @return ESP_OK if loaded successfully, error otherwise.
     * @internal
     */
    virtual esp_err_t load_peers(etl::ivector<PersistentPeer>& peers) = 0;

    /**
     * @brief Stores the peers to storage.
     * @param peers The peers to store.
     * @param force_nvs_commit Force NVS commit after store.
     * @return ESP_OK if stored successfully, error otherwise.
     * @internal
     */
    virtual esp_err_t store_peers(const etl::ivector<PersistentPeer>& peers, bool force_nvs_commit = true) = 0;

    /**
     * @brief Loads all peer statistics from storage.
     * @param stats Output vector to be populated with statistics.
     * @return ESP_OK if loaded successfully, or error code.
     * @internal
     */
    virtual esp_err_t load_stats(etl::ivector<PeerStatisticsPersist>& stats) = 0;

    /**
     * @brief Stores a single peer's statistics to storage.
     * @param stats The statistics entry to store.
     * @return ESP_OK if stored successfully, error otherwise.
     * @internal
     */
    virtual esp_err_t store_stats(const PeerStatisticsPersist& stats) = 0;
};
