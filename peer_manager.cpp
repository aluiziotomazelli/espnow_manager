#include "peer_manager.hpp"
#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include <algorithm>
#include <cstring>

static const char *TAG = "PeerManager";

RealPeerManager::RealPeerManager(IStorage &storage)
    : storage_(storage)
{
    mutex_ = xSemaphoreCreateMutex();
}

RealPeerManager::~RealPeerManager()
{
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

esp_err_t RealPeerManager::add(NodeId id,
                               const uint8_t *mac,
                               uint8_t channel,
                               NodeType type,
                               uint32_t heartbeat_interval_ms)
{
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t result = ESP_OK;

    // Check if peer already exists
    auto it = std::find_if(peers_.begin(), peers_.end(), [id](const PeerInfo &p) { return p.node_id == id; });

    if (it != peers_.end()) {
        ESP_LOGI(TAG, "Node ID %d already exists. Updating peer info.", (int)id);

        bool mac_changed     = (memcmp(it->mac, mac, 6) != 0);
        bool channel_changed = (it->channel != channel);

        if (mac_changed) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, mac, 6);
            peer_info.channel = channel;
            peer_info.ifidx   = WIFI_IF_STA;
            peer_info.encrypt = false;

            result = esp_now_add_peer(&peer_info);

            if (result == ESP_OK) {
                result = esp_now_del_peer(it->mac);
            }
        }
        else if (channel_changed) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, mac, 6);
            peer_info.channel = channel;
            peer_info.ifidx   = WIFI_IF_STA;
            peer_info.encrypt = false;
            result            = esp_now_mod_peer(&peer_info);
        }

        if (result == ESP_OK) {
            memcpy(it->mac, mac, 6);
            it->type                  = type;
            it->channel               = channel;
            it->heartbeat_interval_ms = heartbeat_interval_ms;
            // Move to front (LRU)
            PeerInfo updated = *it;
            peers_.erase(it);
            peers_.insert(peers_.begin(), updated);
        }
    }
    else {
        // New peer
        if (peers_.size() >= MAX_PEERS) {
            ESP_LOGW(TAG, "Peer list is full. Removing the oldest peer.");
            const PeerInfo &oldest = peers_.back();
            esp_now_del_peer(oldest.mac);
            peers_.pop_back();
        }

        esp_now_peer_info_t peer_info = {};
        memcpy(peer_info.peer_addr, mac, 6);
        peer_info.channel = channel;
        peer_info.ifidx   = WIFI_IF_STA;
        peer_info.encrypt = false;
        result            = esp_now_add_peer(&peer_info);

        if (result == ESP_OK) {
            PeerInfo new_peer;
            memcpy(new_peer.mac, mac, 6);
            new_peer.node_id               = id;
            new_peer.type                  = type;
            new_peer.channel               = channel;
            new_peer.last_seen_ms          = 0; // Will be updated by caller if needed
            new_peer.paired                = true;
            new_peer.heartbeat_interval_ms = heartbeat_interval_ms;
            peers_.insert(peers_.begin(), new_peer);
            ESP_LOGI(TAG, "New peer added: ID %d", (int)id);
        }
    }

    if (result == ESP_OK) {
        save_to_storage(channel);
    }

    xSemaphoreGive(mutex_);
    return result;
}

esp_err_t RealPeerManager::remove(NodeId id)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    auto it = std::find_if(peers_.begin(), peers_.end(), [id](const PeerInfo &p) { return p.node_id == id; });

    if (it == peers_.end()) {
        xSemaphoreGive(mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result     = esp_now_del_peer(it->mac);
    uint8_t last_channel = it->channel;
    peers_.erase(it);

    save_to_storage(last_channel);

    xSemaphoreGive(mutex_);
    return result;
}

bool RealPeerManager::find_mac(NodeId id, uint8_t *mac)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool found = false;
    for (const auto &p : peers_) {
        if (p.node_id == id) {
            if (mac)
                memcpy(mac, p.mac, 6);
            found = true;
            break;
        }
    }

    xSemaphoreGive(mutex_);
    return found;
}

std::vector<PeerInfo> RealPeerManager::get_all()
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return {};
    }
    std::vector<PeerInfo> copy = peers_;
    xSemaphoreGive(mutex_);
    return copy;
}

std::vector<NodeId> RealPeerManager::get_offline(uint64_t now_ms)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) {
        return {};
    }

    std::vector<NodeId> offline;
    for (const auto &p : peers_) {
        if (p.heartbeat_interval_ms > 0) {
            uint32_t timeout = p.heartbeat_interval_ms * HEARTBEAT_OFFLINE_MULTIPLIER;
            if (p.last_seen_ms > 0 && (now_ms - p.last_seen_ms > timeout)) {
                offline.push_back(p.node_id);
            }
        }
    }

    xSemaphoreGive(mutex_);
    return offline;
}

void RealPeerManager::update_last_seen(NodeId id, uint64_t now_ms)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        for (auto &p : peers_) {
            if (p.node_id == id) {
                p.last_seen_ms = now_ms;
                break;
            }
        }
        xSemaphoreGive(mutex_);
    }
}

esp_err_t RealPeerManager::load_from_storage(uint8_t &wifi_channel)
{
    std::vector<PersistentPeer> stored_peers;
    esp_err_t err = storage_.load(wifi_channel, stored_peers);
    if (err == ESP_OK) {
        if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
            peers_.clear();
            for (const auto &sp : stored_peers) {
                peers_.push_back(persistent_to_info(sp));
            }
            xSemaphoreGive(mutex_);
        }
    }
    return err;
}

void RealPeerManager::persist(uint8_t wifi_channel)
{
    if (xSemaphoreTake(mutex_, portMAX_DELAY) == pdTRUE) {
        save_to_storage(wifi_channel);
        xSemaphoreGive(mutex_);
    }
}

void RealPeerManager::save_to_storage(uint8_t wifi_channel)
{
    std::vector<PersistentPeer> to_save;
    for (const auto &p : peers_) {
        to_save.push_back(info_to_persistent(p));
    }
    esp_err_t err = storage_.save(wifi_channel, to_save, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save peers to storage: %s", esp_err_to_name(err));
    }
}

PersistentPeer RealPeerManager::info_to_persistent(const PeerInfo &info)
{
    PersistentPeer p;
    memcpy(p.mac, info.mac, 6);
    p.type                  = info.type;
    p.node_id               = info.node_id;
    p.channel               = info.channel;
    p.paired                = info.paired;
    p.heartbeat_interval_ms = info.heartbeat_interval_ms;
    return p;
}

PeerInfo RealPeerManager::persistent_to_info(const PersistentPeer &persistent)
{
    PeerInfo info;
    memcpy(info.mac, persistent.mac, 6);
    info.type                  = persistent.type;
    info.node_id               = persistent.node_id;
    info.channel               = persistent.channel;
    info.last_seen_ms          = 0;
    info.paired                = persistent.paired;
    info.heartbeat_interval_ms = persistent.heartbeat_interval_ms;
    return info;
}
