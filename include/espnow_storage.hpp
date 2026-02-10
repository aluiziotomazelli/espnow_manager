#pragma once

#include "esp_err.h"
#include "protocol_types.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

/**
 * @brief Class to handle persistence of EspNow component data in RTC memory and NVS.
 */
class EspNowStorage
{
public:
    /**
     * @brief Peer information to be persisted.
     */
    struct Peer
    {
        uint8_t mac[6];
        NodeType type;
        NodeId node_id;
        uint8_t channel;
        bool paired;
        uint32_t heartbeat_interval_ms;
    };

    static constexpr size_t MAX_PERSISTENT_PEERS     = 19;
    static constexpr uint32_t ESPNOW_STORAGE_MAGIC   = 0x4553504E;
    static constexpr uint32_t ESPNOW_STORAGE_VERSION = 1;

    EspNowStorage();
    ~EspNowStorage();

    /**
     * @brief Loads data from RTC or NVS.
     *
     * @param wifi_channel Output for the loaded wifi channel.
     * @param peers Output for the loaded peer list.
     * @return ESP_OK if loaded successfully, error otherwise.
     */
    esp_err_t load(uint8_t &wifi_channel, std::vector<Peer> &peers);

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
                   const std::vector<Peer> &peers,
                   bool force_nvs_commit = true);

    /**
     * @brief Internal structure for persistent data.
     */
    struct PersistentData
    {
        uint32_t magic;
        uint32_t version;
        uint8_t wifi_channel;
        uint8_t num_peers;
        Peer peers[MAX_PERSISTENT_PEERS];
        uint32_t crc;
    };

private:
    esp_err_t init_nvs();
    uint32_t calculate_crc(const PersistentData &data);
    static PersistentData rtc_storage;

    // #define UNIT_TESTING 1
public:
#if UNIT_TESTING
    static void test_reset_rtc();
    static PersistentData &test_get_rtc();
    static void test_inject_rtc(const PersistentData &data);
    static uint32_t test_calculate_crc(
        const PersistentData &data); // também estático
#endif
};
