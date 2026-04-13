#include <algorithm>
#include <cstring>

#include "esp_log.h"
// #include "esp_now.h"
// #include "esp_wifi.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
// #include "freertos/task.h"

#include "i_storage_manager.hpp"
#include "i_hal_espnow.hpp"
#include "peer_manager.hpp"

static const char* TAG = "PeerManager";

PeerManager::PeerManager(IStorageManager& storage, IEspNowHAL& hal_espnow, IFreeRTOSHAL& hal_freertos)
    : storage_(storage)
    , hal_espnow_(hal_espnow)
    , hal_freertos_(hal_freertos)
{
    mutex_ = hal_freertos_.mutex_create();
    // peers_ is etl::vector, capacity is fixed at compile-time.
}

PeerManager::~PeerManager()
{
    if (mutex_ != nullptr) {
        hal_freertos_.semaphore_delete(mutex_);
    }
}

// =====================================================================================
// Public methods
// =====================================================================================

esp_err_t PeerManager::add(NodeId id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms)
{
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Find existing peers
    PeerInfo* existing_by_id = find_peer_by_id(id);
    PeerInfo* existing_by_mac = find_peer_by_mac(mac);

    esp_err_t ret = ESP_OK;

    // Case 1: ID exists → update
    if (existing_by_id != nullptr) {
        ret = update_existing_peer_by_id(existing_by_id, mac, type, heartbeat_interval_ms);
    }
    // Case 2: MAC exists with different ID → reassign
    else if (existing_by_mac != nullptr) {
        reassign_mac_to_new_id(existing_by_mac, id, type, heartbeat_interval_ms);
    }
    // Case 3: New peer → add
    else {
        ret = add_new_peer_to_empty_slot(id, mac, type, heartbeat_interval_ms);
    }

    // Save to storage if successful
    if (ret == ESP_OK) {
        ret = save_peers_to_storage();
    }

    hal_freertos_.semaphore_give(mutex_);
    return ret;
}

esp_err_t PeerManager::remove(NodeId id)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    auto it = std::find_if(peers_.begin(), peers_.end(), [id](const PeerInfo& p) { return p.node_id == id; });

    if (it == peers_.end()) {
        hal_freertos_.semaphore_give(mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = hal_espnow_.hal_esp_now_del_peer(it->mac);

    if (ret == ESP_OK) {               // If peer is removed successfully from driver
        peers_.erase(it);              // Remove from peer list
        ret = save_peers_to_storage(); // Save to storage
    }

    hal_freertos_.semaphore_give(mutex_);
    return ret;
}

bool PeerManager::find_mac(NodeId id, uint8_t* mac)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool found = false;
    for (const auto& p : peers_) {
        if (p.node_id == id) {
            if (mac != nullptr) {
                memcpy(mac, p.mac, 6);
            }
            found = true;
            break;
        }
    }

    hal_freertos_.semaphore_give(mutex_);
    return found;
}

etl::vector<PeerInfo, MAX_PEERS> PeerManager::get_all()
{
    etl::vector<PeerInfo, MAX_PEERS> copy;

    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE) {
        return copy;
    }
    copy = peers_;
    hal_freertos_.semaphore_give(mutex_);
    return copy;
}

etl::vector<NodeId, MAX_PEERS> PeerManager::get_offline(int64_t now_ms)
{
    etl::vector<NodeId, MAX_PEERS> offline;

    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE) {
        return offline;
    }

    for (const auto& p : peers_) {
        if (p.heartbeat_interval_ms > 0) {
            uint32_t timeout = p.heartbeat_interval_ms * HEARTBEAT_OFFLINE_MULTIPLIER;
            if (p.last_seen_ms > 0 && (now_ms - p.last_seen_ms > timeout)) {
                offline.push_back(p.node_id);
            }
        }
    }

    hal_freertos_.semaphore_give(mutex_);
    return offline;
}

void PeerManager::update_last_seen(NodeId id, int64_t now_ms)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE) {
        return;
    }
    for (auto& p : peers_) {
        if (p.node_id == id) {
            p.last_seen_ms = now_ms;
            break;
        }
    }
    hal_freertos_.semaphore_give(mutex_);
}

esp_err_t PeerManager::find_node_id_by_mac(const uint8_t* mac, NodeId& out_id)
{
    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    for (const auto& p : peers_) {
        if (memcmp(p.mac, mac, 6) == 0) {
            out_id = p.node_id;
            ret = ESP_OK;
            break;
        }
    }

    hal_freertos_.semaphore_give(mutex_);
    return ret;
}

esp_err_t PeerManager::load_peers_from_storage()
{
    etl::vector<PersistentPeer, MAX_PEERS> stored_peers;

    esp_err_t err = storage_.load_peers(stored_peers);
    if (err != ESP_OK) {
        return err;
    }

    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        peers_.clear();
        for (const auto& sp : stored_peers) {
            if (peers_.size() < MAX_PEERS) {
                peers_.push_back(persistent_to_info(sp));
            }
        }
        hal_freertos_.semaphore_give(mutex_);
        return ESP_OK;
    }
    else {
        return ESP_ERR_TIMEOUT;
    }
}

// =====================================================================================
// Private methods
// =====================================================================================

esp_err_t PeerManager::save_peers_to_storage()
{
    etl::vector<PersistentPeer, MAX_PEERS> peers_to_save;
    for (const auto& p : peers_) {
        peers_to_save.push_back(info_to_persistent(p));
    }
    return storage_.store_peers(peers_to_save, true);
}

PersistentPeer PeerManager::info_to_persistent(const PeerInfo& info)
{
    PersistentPeer p;
    memcpy(p.mac, info.mac, 6);
    p.type = info.type;
    p.node_id = info.node_id;
    p.paired = info.paired;
    p.heartbeat_interval_ms = info.heartbeat_interval_ms;
    return p;
}

PeerInfo PeerManager::persistent_to_info(const PersistentPeer& persistent)
{
    PeerInfo info;
    memcpy(info.mac, persistent.mac, 6);
    info.type = persistent.type;
    info.node_id = persistent.node_id;
    info.last_seen_ms = 0;
    info.paired = persistent.paired;
    info.heartbeat_interval_ms = persistent.heartbeat_interval_ms;
    return info;
}

esp_now_peer_info_t PeerManager::make_espnow_peer_info(const uint8_t* mac)
{
    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel = 0;
    peer_info.ifidx = WIFI_IF_STA;
    peer_info.encrypt = false;
    return peer_info;
}

// =====================================================================================
// Private helper methods for add()
// =====================================================================================

PeerInfo* PeerManager::find_peer_by_id(NodeId id)
{
    for (auto& peer : peers_) {
        if (peer.node_id == id) {
            return &peer;
        }
    }
    return nullptr;
}

PeerInfo* PeerManager::find_peer_by_mac(const uint8_t* mac)
{
    for (auto& peer : peers_) {
        if (memcmp(peer.mac, mac, 6) == 0) {
            return &peer;
        }
    }
    return nullptr;
}

esp_err_t PeerManager::update_existing_peer_by_id(
    PeerInfo* peer,
    const uint8_t* new_mac,
    NodeType type,
    uint32_t heartbeat_interval_ms)
{
    ESP_LOGI(TAG, "Node ID %d already exists. Updating peer info.", (int)peer->node_id);

    bool mac_changed = (memcmp(peer->mac, new_mac, 6) != 0);
    esp_err_t ret = ESP_OK;

    if (mac_changed) {
        // Delete old MAC from ESP-NOW driver
        ret = hal_espnow_.hal_esp_now_del_peer(peer->mac);
        // Add peer with new MAC
        if (ret == ESP_OK) {
            auto peer_info = make_espnow_peer_info(new_mac);
            ret = hal_espnow_.hal_esp_now_add_peer(&peer_info);
        }
    }

    if (ret == ESP_OK) {
        // Update peer info in our list
        memcpy(peer->mac, new_mac, 6);
        peer->type = type;
        peer->heartbeat_interval_ms = heartbeat_interval_ms;

        // Move to front (LRU)
        PeerInfo updated = *peer;
        auto it = std::find_if(peers_.begin(), peers_.end(), [peer](const PeerInfo& p) { return &p == peer; });
        if (it != peers_.end()) {
            peers_.erase(it);
            peers_.insert(peers_.begin(), updated);
        }
    }

    return ret;
}

void PeerManager::reassign_mac_to_new_id(PeerInfo* peer, NodeId new_id, NodeType type, uint32_t heartbeat_interval_ms)
{
    ESP_LOGI(TAG, "MAC address already exists with ID %d. Re-assigning to new ID %d.", (int)peer->node_id, (int)new_id);

    // Update the existing entry with the new ID and other info
    peer->node_id = new_id;
    peer->type = type;
    peer->heartbeat_interval_ms = heartbeat_interval_ms;
    peer->last_seen_ms = 0; // Reset as it's practically a new node identity

    // Move to front (LRU)
    PeerInfo updated = *peer;
    auto it = std::find_if(peers_.begin(), peers_.end(), [peer](const PeerInfo& p) { return &p == peer; });
    if (it != peers_.end()) {
        peers_.erase(it);
        peers_.insert(peers_.begin(), updated);
    }
}

esp_err_t
PeerManager::add_new_peer_to_empty_slot(NodeId id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms)
{
    esp_err_t ret = ESP_OK;

    // Check if peer list is full
    if (peers_.size() >= MAX_PEERS) {
        ESP_LOGW(TAG, "Peer list is full. Removing the oldest seen peer.");

        // Evict based on LRU (least recently seen)
        auto oldest = std::min_element(peers_.begin(), peers_.end(), [](const PeerInfo& a, const PeerInfo& b) {
            return a.last_seen_ms < b.last_seen_ms;
        });

        ret = hal_espnow_.hal_esp_now_del_peer(oldest->mac);
        if (ret == ESP_OK) {
            peers_.erase(oldest);
        }
    }

    // Add new peer to ESP-NOW driver
    if (ret == ESP_OK) {
        auto peer_info = make_espnow_peer_info(mac);
        ret = hal_espnow_.hal_esp_now_add_peer(&peer_info);
    }

    // Create and insert new peer info
    if (ret == ESP_OK) {
        PeerInfo new_peer;
        memcpy(new_peer.mac, mac, 6);
        new_peer.node_id = id;
        new_peer.type = type;
        new_peer.last_seen_ms = 0;
        new_peer.paired = true;
        new_peer.heartbeat_interval_ms = heartbeat_interval_ms;
        peers_.insert(peers_.begin(), new_peer);
        ESP_LOGI(TAG, "New peer added: ID %d", (int)id);
    }

    return ret;
}