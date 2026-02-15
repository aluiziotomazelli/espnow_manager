#pragma once

#include "esp_err.h"
#include "espnow_interfaces.hpp"
#include "protocol_types.hpp"

#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>

/**
 * @brief Internal structure for persistent data.
 */
struct PersistentData
{
    static constexpr size_t MAX_PERSISTENT_PEERS = 19;
    static constexpr uint32_t MAGIC = 0x4553504E;
    static constexpr uint32_t VERSION = 1;

    uint32_t magic;
    uint32_t version;
    uint8_t wifi_channel;
    uint8_t num_peers;
    PersistentPeer peers[MAX_PERSISTENT_PEERS];
    uint32_t crc;
};

/**
 * @brief Class to handle persistence of EspNowManager component data in RTC memory and NVS.
 */
class EspNowStorage : public IStorage
{
public:
    EspNowStorage(std::unique_ptr<IPersistenceBackend> rtc_backend = nullptr,
                  std::unique_ptr<IPersistenceBackend> nvs_backend = nullptr);
    ~EspNowStorage();

    /**
     * @brief Loads data from RTC or NVS.
     *
     * @param wifi_channel Output for the loaded wifi channel.
     * @param peers Output for the loaded peer list.
     * @return ESP_OK if loaded successfully, error otherwise.
     */
    esp_err_t load(uint8_t &wifi_channel, std::vector<PersistentPeer> &peers) override;

    /**
     * @brief Saves data to RTC and NVS.
     *
     * @param wifi_channel Current wifi channel.
     * @param peers Current peer list.
     * @param force_nvs_commit If true, forces a save to NVS even if data seems
     * unchanged.
     * @return ESP_OK if saved successfully, error otherwise.
     */
    esp_err_t save(uint8_t wifi_channel,
                   const std::vector<PersistentPeer> &peers,
                   bool force_nvs_commit = true) override;

private:
    uint32_t calculate_crc(const PersistentData &data);

    std::unique_ptr<IPersistenceBackend> rtc_backend_;
    std::unique_ptr<IPersistenceBackend> nvs_backend_;
};
