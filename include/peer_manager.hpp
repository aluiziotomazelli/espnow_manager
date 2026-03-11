#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "i_peer_manager.hpp"
#include "i_wifi_hal.hpp"
#include "i_storage_manager.hpp"

class PeerManager : public IPeerManager
{
public:
    PeerManager(IStorageManager &storage, IWiFiHAL &driver_hal);

    ~PeerManager();

    using IPeerManager::add;
    using IPeerManager::find_mac;
    using IPeerManager::remove;
    using IPeerManager::update_last_seen;

    esp_err_t add(NodeId id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms = 0) override;
    esp_err_t remove(NodeId id) override;
    bool find_mac(NodeId id, uint8_t *mac) override;
    std::vector<PeerInfo> get_all() override;
    std::vector<NodeId> get_offline(uint64_t now_ms) override;
    void update_last_seen(NodeId id, uint64_t now_ms) override;

    // Helper for initialization (loading from storage)
    esp_err_t load_from_storage(uint8_t &wifi_channel) override;
    void persist() override;
    void set_channel(uint8_t channel) override;

private:
    IStorageManager &storage_;
    IWiFiHAL &driver_hal_;

    std::vector<PeerInfo> peers_;
    SemaphoreHandle_t mutex_;
    uint8_t current_channel_ = 0;

    void save_to_storage();
    PersistentPeer info_to_persistent(const PeerInfo &info);
    PeerInfo persistent_to_info(const PersistentPeer &persistent);
};
