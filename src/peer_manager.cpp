#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "i_storage_manager.hpp"
#include "peer_manager.hpp"

static const char *TAG = "PeerManager";

PeerManager::PeerManager(IStorageManager &storage, IWiFiHAL &driver_hal, IFreeRTOSHAL &freertos_hal)
    : storage_(storage)
    , driver_hal_(driver_hal)
    , freertos_hal_(freertos_hal)
{
    mutex_ = freertos_hal_.mutex_create();
}

PeerManager::~PeerManager()
{
    if (mutex_) {
        freertos_hal_.semaphore_delete(mutex_);
    }
}

esp_err_t
PeerManager::add(NodeId id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms) // TODO: Verify channel
{
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    // Check if peer already exists
    auto it = std::find_if(peers_.begin(), peers_.end(), [id](const PeerInfo &p) { return p.node_id == id; });

    if (it != peers_.end()) {
        ESP_LOGI(TAG, "Node ID %d already exists. Updating peer info.", (int)id);

        bool mac_changed = (memcmp(it->mac, mac, 6) != 0);
        bool channel_changed = (it->channel != current_channel_);

        if (mac_changed) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, mac, 6);
            peer_info.channel = current_channel_;
            peer_info.ifidx = WIFI_IF_STA;
            peer_info.encrypt = false;

            ret = driver_hal_.hal_esp_now_add_peer(&peer_info);

            if (ret == ESP_OK) {
                ret = driver_hal_.hal_esp_now_del_peer(it->mac);
            }
        }
        else if (channel_changed) {
            esp_now_peer_info_t peer_info = {};
            memcpy(peer_info.peer_addr, mac, 6);
            peer_info.channel = current_channel_;
            peer_info.ifidx = WIFI_IF_STA;
            peer_info.encrypt = false;
            ret = driver_hal_.hal_esp_now_mod_peer(&peer_info);
        }

        if (ret == ESP_OK) {
            memcpy(it->mac, mac, 6);
            it->type = type;
            it->channel = current_channel_;
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
            ESP_LOGW(TAG, "Peer list is full. Removing the last seen peer.");

            // Returns a iterator to the element with the smallest last_seen_ms
            auto oldest = std::min_element(peers_.begin(), peers_.end(), [](const PeerInfo &a, const PeerInfo &b) {
                return a.last_seen_ms < b.last_seen_ms;
            });

            driver_hal_.hal_esp_now_del_peer(oldest->mac); // TODO: check return before erasing
            peers_.erase(oldest);
        }

        esp_now_peer_info_t peer_info = {};
        memcpy(peer_info.peer_addr, mac, 6);
        peer_info.channel = current_channel_;
        peer_info.ifidx = WIFI_IF_STA;
        peer_info.encrypt = false;
        ret = driver_hal_.hal_esp_now_add_peer(&peer_info);

        if (ret == ESP_OK) {
            PeerInfo new_peer;
            memcpy(new_peer.mac, mac, 6);
            new_peer.node_id = id;
            new_peer.type = type;
            new_peer.channel = current_channel_;
            new_peer.last_seen_ms = 0; // Will be updated by caller if needed
            new_peer.paired = true;
            new_peer.heartbeat_interval_ms = heartbeat_interval_ms;
            peers_.insert(peers_.begin(), new_peer);
            ESP_LOGI(TAG, "New peer added: ID %d", (int)id);
        }
    }

    if (ret == ESP_OK) {
        save_to_storage();
    }

    freertos_hal_.semaphore_give(mutex_);
    return ret;
}

esp_err_t PeerManager::remove(NodeId id)
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    auto it = std::find_if(peers_.begin(), peers_.end(), [id](const PeerInfo &p) { return p.node_id == id; });

    if (it == peers_.end()) {
        freertos_hal_.semaphore_give(mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = driver_hal_.hal_esp_now_del_peer(it->mac);

    if (ret == ESP_OK) {   // If peer is removed successfully from driver
        peers_.erase(it);  // Remove from peer list
        save_to_storage(); // Save to storage
    }

    freertos_hal_.semaphore_give(mutex_);
    return ret;
}

bool PeerManager::find_mac(NodeId id, uint8_t *mac)
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) != pdTRUE) {
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

    freertos_hal_.semaphore_give(mutex_);
    return found;
}

std::vector<PeerInfo> PeerManager::get_all()
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) != pdTRUE) {
        return {};
    }

    std::vector<PeerInfo> copy = peers_;
    freertos_hal_.semaphore_give(mutex_);
    return copy;
}

std::vector<NodeId> PeerManager::get_offline(uint64_t now_ms)
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) != pdTRUE) {
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

    freertos_hal_.semaphore_give(mutex_);
    return offline;
}

void PeerManager::update_last_seen(NodeId id, uint64_t now_ms)
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) != pdTRUE) {
        return;
    }
    for (auto &p : peers_) {
        if (p.node_id == id) {
            p.last_seen_ms = now_ms;
            break;
        }
    }
    freertos_hal_.semaphore_give(mutex_);
}

esp_err_t PeerManager::load_from_storage(uint8_t &wifi_channel)
{
    std::vector<PersistentPeer> stored_peers;
    esp_err_t err = storage_.load(wifi_channel, stored_peers);
    if (err == ESP_OK) {
        if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) == pdTRUE) {
            peers_.clear();
            for (const auto &sp : stored_peers) {
                peers_.push_back(persistent_to_info(sp));
            }
            freertos_hal_.semaphore_give(mutex_);
        }
        else {
            err = ESP_ERR_TIMEOUT;
        }
    }
    return err;
}

void PeerManager::persist()
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) == pdTRUE) {
        save_to_storage();
        freertos_hal_.semaphore_give(mutex_);
    }
}

void PeerManager::save_to_storage()
{
    std::vector<PersistentPeer> to_save;
    for (const auto &p : peers_) {
        to_save.push_back(info_to_persistent(p));
    }
    esp_err_t err = storage_.save(current_channel_, to_save, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save peers to storage: %s", esp_err_to_name(err));
    }
}

PersistentPeer PeerManager::info_to_persistent(const PeerInfo &info)
{
    PersistentPeer p;
    memcpy(p.mac, info.mac, 6);
    p.type = info.type;
    p.node_id = info.node_id;
    p.channel = info.channel;
    p.paired = info.paired;
    p.heartbeat_interval_ms = info.heartbeat_interval_ms;
    return p;
}

PeerInfo PeerManager::persistent_to_info(const PersistentPeer &persistent)
{
    PeerInfo info;
    memcpy(info.mac, persistent.mac, 6);
    info.type = persistent.type;
    info.node_id = persistent.node_id;
    info.channel = persistent.channel;
    info.last_seen_ms = 0;
    info.paired = persistent.paired;
    info.heartbeat_interval_ms = persistent.heartbeat_interval_ms;
    return info;
}

void PeerManager::set_channel(uint8_t channel)
{
    if (freertos_hal_.semaphore_take(mutex_, PORT_MAX_DELAY) == pdTRUE) {
        current_channel_ = channel;
        freertos_hal_.semaphore_give(mutex_);
    }
}