#pragma once

#include "etl/vector.h"

#include "i_peer_manager.hpp"
#include "i_hal_espnow.hpp"
#include "i_storage_manager.hpp"
#include "i_hal_freertos.hpp"

class PeerManager : public IPeerManager
{
public:
    PeerManager(IStorageManager& storage, IEspNowHAL& espnow_hal, IFreeRTOSHAL& freertos_hal);

    ~PeerManager();

    using IPeerManager::add;
    using IPeerManager::find_mac;
    using IPeerManager::remove;
    using IPeerManager::update_last_seen;

    /** @copydoc IPeerManager::add */
    esp_err_t add(NodeId id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms = 0) override;

    /** @copydoc IPeerManager::remove */
    esp_err_t remove(NodeId id) override;

    /** @copydoc IPeerManager::find_mac */
    bool find_mac(NodeId id, uint8_t* mac) override;

    /** @copydoc IPeerManager::get_all */
    etl::vector<PeerInfo, MAX_PEERS> get_all() override;

    /** @copydoc IPeerManager::get_offline */
    etl::vector<NodeId, MAX_PEERS> get_offline(int64_t now_ms) override;

    /** @copydoc IPeerManager::update_last_seen */
    void update_last_seen(NodeId id, int64_t now_ms) override;

    /** @copydoc IPeerManager::find_node_id_by_mac */
    esp_err_t find_node_id_by_mac(const uint8_t* mac, NodeId& out_id) override;

    // Helper for initialization (loading from storage)
    /** @copydoc IPeerManager::load_peers_from_storage */
    esp_err_t load_peers_from_storage() override;

private:
    IStorageManager& storage_;
    IEspNowHAL& hal_espnow_;
    IFreeRTOSHAL& hal_freertos_;

    etl::vector<PeerInfo, MAX_PEERS> peers_;
    SemaphoreHandle_t mutex_;

    // Helper methods for add() refactoring
    PeerInfo* find_peer_by_id(NodeId id);
    PeerInfo* find_peer_by_mac(const uint8_t* mac);
    esp_err_t update_existing_peer_by_id(PeerInfo* peer, const uint8_t* new_mac, NodeType type, uint32_t heartbeat_interval_ms);
    void reassign_mac_to_new_id(PeerInfo* peer, NodeId new_id, NodeType type, uint32_t heartbeat_interval_ms);
    esp_err_t add_new_peer_to_empty_slot(NodeId id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms);

    PersistentPeer info_to_persistent(const PeerInfo& info);
    PeerInfo persistent_to_info(const PersistentPeer& persistent);
    esp_now_peer_info_t make_espnow_peer_info(const uint8_t* mac);
};
