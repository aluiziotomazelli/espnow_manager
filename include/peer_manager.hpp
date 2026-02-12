#pragma once

#include "espnow_interfaces.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <vector>

class RealPeerManager : public IPeerManager
{
public:
    RealPeerManager(IStorage &storage);
    ~RealPeerManager();

    esp_err_t add(NodeId id, const uint8_t *mac, uint8_t channel, NodeType type, uint32_t heartbeat_interval_ms = 0) override;
    esp_err_t remove(NodeId id) override;
    bool find_mac(NodeId id, uint8_t *mac) override;
    std::vector<PeerInfo> get_all() override;
    std::vector<NodeId> get_offline(uint64_t now_ms) override;
    void update_last_seen(NodeId id, uint64_t now_ms) override;

    // Helper for initialization (loading from storage)
    esp_err_t load_from_storage(uint8_t &wifi_channel) override;
    void persist(uint8_t wifi_channel) override;

private:
    IStorage &storage_;
    std::vector<PeerInfo> peers_;
    SemaphoreHandle_t mutex_;

    void save_to_storage(uint8_t wifi_channel);
    PersistentPeer info_to_persistent(const PeerInfo &info);
    PeerInfo persistent_to_info(const PersistentPeer &persistent);
};
