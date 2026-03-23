#pragma once

#include "etl/vector.h"

#include "i_peer_manager.hpp"
#include "i_hal_wifi.hpp"
#include "i_storage_manager.hpp"
#include "i_hal_freertos.hpp"

class PeerManager : public IPeerManager
{
public:
    PeerManager(IStorageManager &storage, IWiFiHAL &driver_hal, IFreeRTOSHAL &freertos_hal);

    ~PeerManager();

    using IPeerManager::add;
    using IPeerManager::find_mac;
    using IPeerManager::remove;
    using IPeerManager::update_last_seen;

    esp_err_t add(NodeId id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms = 0) override;
    esp_err_t remove(NodeId id) override;
    bool find_mac(NodeId id, uint8_t *mac) override;
    etl::vector<PeerInfo, MAX_PEERS> get_all() override;
    etl::vector<NodeId, MAX_PEERS> get_offline(uint64_t now_ms) override;
    void update_last_seen(NodeId id, uint64_t now_ms) override;

    // Helper for initialization (loading from storage)
    esp_err_t load_channel_from_storage(uint8_t &channel) override;
    esp_err_t save_channel_in_storage(uint8_t channel) override;
    esp_err_t load_peers_from_storage() override;

private:
    IStorageManager &storage_;
    IWiFiHAL &driver_hal_;
    IFreeRTOSHAL &freertos_hal_;

    etl::vector<PeerInfo, MAX_PEERS> peers_;
    SemaphoreHandle_t mutex_;

    esp_err_t save_peers_to_storage();
    PersistentPeer info_to_persistent(const PeerInfo &info);
    PeerInfo persistent_to_info(const PersistentPeer &persistent);
    esp_now_peer_info_t make_espnow_peer_info(const uint8_t *mac);
};
