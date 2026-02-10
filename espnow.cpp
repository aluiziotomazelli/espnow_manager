#include "espnow.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "protocol_messages.hpp"
#include <algorithm>
#include <cstring>
#include <inttypes.h>

// Logging TAG
static const char *TAG = "EspNow";

// Singleton static members
EspNow *EspNow::instance_ptr_              = nullptr;
SemaphoreHandle_t EspNow::singleton_mutex_ = nullptr;

// --- Singleton ---
EspNow &EspNow::instance()
{
    if (instance_ptr_ == nullptr) {
        if (singleton_mutex_ == nullptr) {
            singleton_mutex_ = xSemaphoreCreateMutex();
        }

        if (xSemaphoreTake(singleton_mutex_, portMAX_DELAY) == pdTRUE) {
            if (instance_ptr_ == nullptr) {
                instance_ptr_ = new EspNow();
            }
            xSemaphoreGive(singleton_mutex_);
        }
    }
    return *instance_ptr_;
}

// --- Constructor & Destructor ---
EspNow::EspNow()
{
}

EspNow::~EspNow()
{
    deinit();
    if (singleton_mutex_ != nullptr) {
        vSemaphoreDelete(singleton_mutex_);
        singleton_mutex_ = nullptr;
    }
    ESP_LOGI(TAG, "Resources released.");
}

esp_err_t EspNow::deinit()
{
    if (!is_initialized_) {
        return ESP_OK;
    }

    is_initialized_ = false;
    ESP_LOGI(TAG, "Deinitializing EspNow component...");

    if (rx_dispatch_task_handle_ != nullptr) {
        xTaskNotify(rx_dispatch_task_handle_, NOTIFY_STOP, eSetBits);
    }
    if (transport_worker_task_handle_ != nullptr) {
        xTaskNotify(transport_worker_task_handle_, NOTIFY_STOP, eSetBits);
    }
    if (tx_manager_task_handle_ != nullptr) {
        xTaskNotify(tx_manager_task_handle_, NOTIFY_STOP, eSetBits);
    }

    // Give tasks time to exit
    vTaskDelay(pdMS_TO_TICKS(100));

    // Force delete any remaining tasks
    if (rx_dispatch_task_handle_ != nullptr) {
        vTaskDelete(rx_dispatch_task_handle_);
        rx_dispatch_task_handle_ = nullptr;
    }
    if (transport_worker_task_handle_ != nullptr) {
        vTaskDelete(transport_worker_task_handle_);
        transport_worker_task_handle_ = nullptr;
    }
    if (tx_manager_task_handle_ != nullptr) {
        vTaskDelete(tx_manager_task_handle_);
        tx_manager_task_handle_ = nullptr;
    }
    if (heartbeat_timer_handle_ != nullptr) {
        xTimerDelete(heartbeat_timer_handle_, portMAX_DELAY);
        heartbeat_timer_handle_ = nullptr;
    }
    if (ack_timeout_timer_handle_ != nullptr) {
        xTimerDelete(ack_timeout_timer_handle_, portMAX_DELAY);
        ack_timeout_timer_handle_ = nullptr;
    }
    if (pairing_timer_handle_ != nullptr) {
        xTimerDelete(pairing_timer_handle_, portMAX_DELAY);
        pairing_timer_handle_ = nullptr;
    }
    if (pairing_timeout_timer_handle_ != nullptr) {
        xTimerDelete(pairing_timeout_timer_handle_, portMAX_DELAY);
        pairing_timeout_timer_handle_ = nullptr;
    }

    if (rx_dispatch_queue_ != nullptr) {
        vQueueDelete(rx_dispatch_queue_);
        rx_dispatch_queue_ = nullptr;
    }
    if (transport_worker_queue_ != nullptr) {
        vQueueDelete(transport_worker_queue_);
        transport_worker_queue_ = nullptr;
    }
    if (tx_queue_ != nullptr) {
        vQueueDelete(tx_queue_);
        tx_queue_ = nullptr;
    }

    esp_now_deinit();

    if (peers_mutex_ != nullptr) {
        vSemaphoreDelete(peers_mutex_);
        peers_mutex_ = nullptr;
    }
    if (pairing_mutex_ != nullptr) {
        vSemaphoreDelete(pairing_mutex_);
        pairing_mutex_ = nullptr;
    }
    if (ack_mutex_ != nullptr) {
        vSemaphoreDelete(ack_mutex_);
        ack_mutex_ = nullptr;
    }

    is_initialized_ = false;
    ESP_LOGI(TAG, "EspNow component deinitialized.");
    return ESP_OK;
}

// --- Public API ---
esp_err_t EspNow::init(const EspNowConfig &config)
{
    if (is_initialized_) {
        ESP_LOGW(TAG, "Already initialized.");
        return ESP_ERR_INVALID_STATE;
    }

    if (config.app_rx_queue == nullptr) {
        ESP_LOGE(TAG, "Application RX queue cannot be null.");
        return ESP_ERR_INVALID_ARG;
    }

    config_ = config;

    // Ensure Wi-Fi mode is set before initializing ESP-NOW
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK || mode == WIFI_MODE_NULL) {
        ESP_LOGE(TAG,
                 "Wi-Fi mode is NULL or not set. Initialize Wi-Fi before EspNow.");
        return ESP_ERR_INVALID_STATE;
    }

    // Persistence: Load peers and channel
    std::vector<EspNowStorage::Peer> stored_peers;
    uint8_t stored_channel;
    bool has_stored_data = false;
    if (storage_.load(stored_channel, stored_peers) == ESP_OK) {
        config_.wifi_channel = stored_channel;
        has_stored_data      = true;
        ESP_LOGI(TAG, "Persistence: Loaded channel %d and %d peers", stored_channel,
                 (int)stored_peers.size());
    }

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(esp_now_recv_cb));
    ESP_ERROR_CHECK(esp_now_register_send_cb(esp_now_send_cb));

    // Ensure Wi-Fi hardware is on the configured channel
    ESP_ERROR_CHECK(
        esp_wifi_set_channel(config_.wifi_channel, WIFI_SECOND_CHAN_NONE));

    esp_now_peer_info_t broadcast_peer = {};
    const uint8_t broadcast_mac[]      = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
    broadcast_peer.channel = config_.wifi_channel;
    broadcast_peer.ifidx   = WIFI_IF_STA;
    broadcast_peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&broadcast_peer));

    peers_mutex_   = xSemaphoreCreateMutex();
    pairing_mutex_ = xSemaphoreCreateMutex();
    ack_mutex_     = xSemaphoreCreateMutex();
    if (peers_mutex_ == nullptr || pairing_mutex_ == nullptr ||
        ack_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutexes.");
        return ESP_FAIL;
    }

    rx_dispatch_queue_      = xQueueCreate(30, sizeof(RxPacket));
    transport_worker_queue_ = xQueueCreate(20, sizeof(RxPacket));
    tx_queue_               = xQueueCreate(20, sizeof(TxPacket));
    if (rx_dispatch_queue_ == nullptr || transport_worker_queue_ == nullptr ||
        tx_queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create internal queues.");
        return ESP_FAIL;
    }

    if (xTaskCreate(rx_dispatch_task, "espnow_dispatch",
                    config_.stack_size_rx_dispatch, this, 10,
                    &rx_dispatch_task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create dispatch task.");
        return ESP_FAIL;
    }

    if (xTaskCreate(transport_worker_task, "espnow_worker",
                    config_.stack_size_transport_worker, this, 5,
                    &transport_worker_task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create worker task.");
        return ESP_FAIL;
    }

    if (xTaskCreate(tx_manager_task, "espnow_tx_manager",
                    config_.stack_size_tx_manager, this, 9,
                    &tx_manager_task_handle_) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TX manager task.");
        return ESP_FAIL;
    }

    is_initialized_ = true;

    // Restore peers from storage
    if (has_stored_data && !stored_peers.empty()) {
        if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
            for (const auto &sp : stored_peers) {
                PeerInfo info = storage_to_info(sp);

                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, info.mac, 6);
                peer_info.channel    = info.channel;
                peer_info.ifidx      = WIFI_IF_STA;
                peer_info.encrypt    = false;
                esp_err_t add_result = esp_now_add_peer(&peer_info);
                if (add_result == ESP_OK) {
                    info.last_seen_ms = get_time_ms();
                    peers_.push_back(info);
                    ESP_LOGI(TAG, "Persistence: Restored peer " MACSTR,
                             MAC2STR(info.mac));
                }
                else {
                    ESP_LOGE(TAG,
                             "Persistence: Failed to restore peer " MACSTR ": %s",
                             MAC2STR(info.mac), esp_err_to_name(add_result));
                }
            }
            xSemaphoreGive(peers_mutex_);
        }
    }

    ESP_LOGI(TAG, "EspNow component initialized successfully.");

    if (config_.node_type != NodeType::HUB && config_.heartbeat_interval_ms > 0) {
        ESP_LOGI(TAG, "Starting heartbeat timer with interval: %" PRIu32 " ms.",
                 config_.heartbeat_interval_ms);
        heartbeat_timer_handle_ =
            xTimerCreate("heartbeat", pdMS_TO_TICKS(config_.heartbeat_interval_ms),
                         pdTRUE, this, periodic_heartbeat_cb);
        if (heartbeat_timer_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create heartbeat timer.");
            return ESP_FAIL;
        }
        xTimerStart(heartbeat_timer_handle_, 0);
    }

    return ESP_OK;
}

esp_err_t EspNow::send_data(NodeId dest_node_id,
                            PayloadType payload_type,
                            const void *payload,
                            size_t len,
                            bool require_ack)
{
    TxPacket tx_packet;
    if (!find_peer_mac(dest_node_id, tx_packet.dest_mac)) {
        ESP_LOGE(TAG, "Could not find peer with node_id: %" PRIu8,
                 static_cast<uint8_t>(dest_node_id));
        return ESP_ERR_NOT_FOUND;
    }

    tx_packet.len = sizeof(MessageHeader) + len;
    if (tx_packet.len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    tx_packet.requires_ack = require_ack;

    MessageHeader *header   = reinterpret_cast<MessageHeader *>(tx_packet.data);
    header->msg_type        = MessageType::DATA;
    header->sequence_number = 0; // The TX task will assign this
    header->sender_type     = config_.node_type;
    header->sender_node_id  = config_.node_id;
    header->payload_type    = payload_type;
    header->requires_ack    = require_ack;
    header->dest_node_id    = dest_node_id;
    header->timestamp_ms    = get_time_ms();

    memcpy(tx_packet.data + sizeof(MessageHeader), payload, len);

    if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue data packet for sending.");
        return ESP_ERR_TIMEOUT;
    }

    if (tx_manager_task_handle_ != nullptr) {
        xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
    }

    return ESP_OK;
}

esp_err_t EspNow::send_command(NodeId dest_node_id,
                               CommandType command_type,
                               const void *payload,
                               size_t len,
                               bool require_ack)
{
    TxPacket tx_packet;
    if (!find_peer_mac(dest_node_id, tx_packet.dest_mac)) {
        ESP_LOGE(TAG, "Could not find peer with node_id: %" PRIu8,
                 static_cast<uint8_t>(dest_node_id));
        return ESP_ERR_NOT_FOUND;
    }

    tx_packet.len = sizeof(MessageHeader) + len;
    if (tx_packet.len > ESP_NOW_MAX_DATA_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    tx_packet.requires_ack = require_ack;

    MessageHeader *header   = reinterpret_cast<MessageHeader *>(tx_packet.data);
    header->msg_type        = MessageType::COMMAND;
    header->sequence_number = 0; // The TX task will assign this
    header->sender_type     = config_.node_type;
    header->sender_node_id  = config_.node_id;
    header->payload_type    = static_cast<PayloadType>(command_type);
    header->requires_ack    = require_ack;
    header->dest_node_id    = dest_node_id;
    header->timestamp_ms    = get_time_ms();

    memcpy(tx_packet.data + sizeof(MessageHeader), payload, len);

    if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue command packet for sending.");
        return ESP_ERR_TIMEOUT;
    }

    if (tx_manager_task_handle_ != nullptr) {
        xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
    }

    return ESP_OK;
}

std::vector<EspNow::PeerInfo> EspNow::get_peers()
{
    std::vector<PeerInfo> peers_copy;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        peers_copy = peers_;
        xSemaphoreGive(peers_mutex_);
    }
    return peers_copy;
}

std::vector<NodeId> EspNow::get_offline_peers() const
{
    std::vector<NodeId> offline_peers;
    if (xSemaphoreTake(peers_mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
        const uint32_t now_ms = get_time_ms();
        for (const auto &peer : peers_) {
            if (peer.heartbeat_interval_ms > 0) {
                uint32_t timeout =
                    peer.heartbeat_interval_ms * HEARTBEAT_OFFLINE_MULTIPLIER;
                // Only consider offline if we have seen it at least once
                // (last_seen_ms > 0) and the timeout has expired.
                if (peer.last_seen_ms > 0 &&
                    (now_ms - peer.last_seen_ms > timeout)) {
                    offline_peers.push_back(peer.node_id);
                }
            }
        }
        xSemaphoreGive(peers_mutex_);
    }
    else {
        ESP_LOGW(TAG, "get_offline_peers: Could not acquire mutex.");
    }
    return offline_peers;
}

esp_err_t EspNow::add_peer(NodeId node_id,
                           const uint8_t *mac,
                           uint8_t channel,
                           NodeType type)
{
    if (mac == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t result = ESP_FAIL;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        result = add_peer_internal(node_id, mac, channel, type);
        if (result == ESP_OK) {
            save_peers(true);
        }
        xSemaphoreGive(peers_mutex_);
    }
    return result;
}

esp_err_t EspNow::start_pairing(uint32_t timeout_ms)
{
    if (xSemaphoreTake(pairing_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (is_pairing_active_) {
        ESP_LOGW(TAG, "Pairing is already active.");
        xSemaphoreGive(pairing_mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Pairing started for %" PRIu32 " ms.", timeout_ms);

    if (pairing_timer_handle_ != nullptr) {
        xTimerDelete(pairing_timer_handle_, portMAX_DELAY);
        pairing_timer_handle_ = nullptr;
    }
    if (pairing_timeout_timer_handle_ != nullptr) {
        xTimerDelete(pairing_timeout_timer_handle_, portMAX_DELAY);
        pairing_timeout_timer_handle_ = nullptr;
    }

    if (config_.node_type == NodeType::HUB) {
        pairing_timeout_timer_handle_ =
            xTimerCreate("pairing_timeout", pdMS_TO_TICKS(timeout_ms), pdFALSE, this,
                         pairing_timer_cb);
        if (pairing_timeout_timer_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create pairing timeout timer.");
            xSemaphoreGive(pairing_mutex_);
            return ESP_FAIL;
        }
        xTimerStart(pairing_timeout_timer_handle_, 0);
    }
    else { // Slave
        pairing_timer_handle_ = xTimerCreate("pairing_periodic", pdMS_TO_TICKS(5000),
                                             pdTRUE, this, periodic_pairing_cb);
        pairing_timeout_timer_handle_ =
            xTimerCreate("pairing_timeout", pdMS_TO_TICKS(timeout_ms), pdFALSE, this,
                         pairing_timer_cb);

        if (pairing_timer_handle_ == nullptr ||
            pairing_timeout_timer_handle_ == nullptr) {
            ESP_LOGE(TAG, "Failed to create slave pairing timers.");
            xSemaphoreGive(pairing_mutex_);
            return ESP_FAIL;
        }
        xTimerStart(pairing_timer_handle_, 0);
        xTimerStart(pairing_timeout_timer_handle_, 0);
        send_pair_request();
    }

    is_pairing_active_ = true;
    xSemaphoreGive(pairing_mutex_);
    return ESP_OK;
}

esp_err_t EspNow::remove_peer(NodeId node_id)
{
    esp_err_t result = ESP_FAIL;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        result = remove_peer_internal(node_id);
        if (result == ESP_OK) {
            save_peers(true);
        }
        xSemaphoreGive(peers_mutex_);
    }
    return result;
}

esp_err_t EspNow::confirm_reception(AckStatus status)
{
    if (xSemaphoreTake(ack_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    if (!last_header_requiring_ack_.has_value()) {
        ESP_LOGW(TAG, "confirm_reception called but no message is awaiting ACK.");
        xSemaphoreGive(ack_mutex_);
        return ESP_ERR_INVALID_STATE;
    }

    const auto &header_to_ack = last_header_requiring_ack_.value();

    AckMessage ack;
    ack.header.msg_type       = MessageType::ACK;
    ack.header.sender_node_id = config_.node_id;
    ack.header.sender_type    = config_.node_type;
    ack.header.dest_node_id   = header_to_ack.sender_node_id;
    ack.ack_sequence          = header_to_ack.sequence_number;
    ack.status                = status;

    TxPacket tx_packet;
    if (!find_peer_mac(header_to_ack.sender_node_id, tx_packet.dest_mac)) {
        ESP_LOGE(TAG, "Cannot send ACK. Peer %d not found.",
                 static_cast<int>(header_to_ack.sender_node_id));
        last_header_requiring_ack_.reset();
        xSemaphoreGive(ack_mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    tx_packet.len = sizeof(ack);
    memcpy(tx_packet.data, &ack, tx_packet.len);
    tx_packet.requires_ack = false;

    if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue ACK packet for sending.");
        last_header_requiring_ack_.reset();
        xSemaphoreGive(ack_mutex_);
        return ESP_ERR_TIMEOUT;
    }

    if (tx_manager_task_handle_ != nullptr) {
        xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
    }

    last_header_requiring_ack_.reset();
    xSemaphoreGive(ack_mutex_);

    return ESP_OK;
}

// --- Private Methods ---
bool EspNow::find_peer_mac(NodeId node_id, uint8_t *mac)
{
    bool peer_found = false;
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        for (const auto &peer : peers_) {
            if (peer.node_id == node_id) {
                memcpy(mac, peer.mac, 6);
                peer_found = true;
                break;
            }
        }
        xSemaphoreGive(peers_mutex_);
    }
    return peer_found;
}

esp_err_t EspNow::send_packet(const uint8_t *mac_addr, const void *data, size_t len)
{
    if (len > ESP_NOW_MAX_DATA_LEN - CRC_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    std::vector<uint8_t> buffer(len + CRC_SIZE);
    memcpy(buffer.data(), data, len);
    buffer.back() = esp_rom_crc8_le(0, buffer.data(), len);

    esp_err_t result = esp_now_send(mac_addr, buffer.data(), buffer.size());
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send packet to %02X:%02X:%02X:%02X:%02X:%02X: %s",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4],
                 mac_addr[5], esp_err_to_name(result));
    }
    return result;
}

esp_err_t EspNow::remove_peer_internal(NodeId node_id)
{
    auto it =
        std::find_if(peers_.begin(), peers_.end(),
                     [node_id](const PeerInfo &p) { return p.node_id == node_id; });

    if (it == peers_.end()) {
        ESP_LOGW(TAG, "Could not find peer with node_id: %" PRIu8,
                 static_cast<uint8_t>(node_id));
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t result = esp_now_del_peer(it->mac);
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to remove peer from ESP-NOW: %s",
                 esp_err_to_name(result));
    }

    ESP_LOGI(TAG, "Removed peer %02X:%02X:%02X:%02X:%02X:%02X (ID: %" PRIu8 ")",
             it->mac[0], it->mac[1], it->mac[2], it->mac[3], it->mac[4], it->mac[5],
             static_cast<uint8_t>(node_id));

    peers_.erase(it);
    return ESP_OK;
}

esp_err_t EspNow::add_peer_internal(NodeId node_id,
                                    const uint8_t *mac,
                                    uint8_t channel,
                                    NodeType type)
{
    for (auto it = peers_.begin(); it != peers_.end(); ++it) {
        if (it->node_id == node_id) {
            ESP_LOGI(TAG, "Node ID %" PRIu8 " already exists. Updating peer info.",
                     static_cast<uint8_t>(node_id));
            PeerInfo updated_peer = *it;
            bool mac_changed      = (memcmp(updated_peer.mac, mac, 6) != 0);
            bool channel_changed  = (updated_peer.channel != channel);

            if (mac_changed) {
                // Restore error handling for deleting the old peer
                esp_err_t del_result = esp_now_del_peer(updated_peer.mac);
                if (del_result != ESP_OK && del_result != ESP_ERR_ESPNOW_NOT_FOUND) {
                    ESP_LOGE(TAG, "Failed to remove old MAC for peer %" PRIu8 ": %s",
                             static_cast<uint8_t>(node_id),
                             esp_err_to_name(del_result));
                }

                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel    = channel;
                peer_info.ifidx      = WIFI_IF_STA;
                peer_info.encrypt    = false;
                esp_err_t add_result = esp_now_add_peer(&peer_info);
                if (add_result != ESP_OK) {
                    return add_result;
                }
            }
            else if (channel_changed) {
                esp_now_peer_info_t peer_info = {};
                memcpy(peer_info.peer_addr, mac, 6);
                peer_info.channel    = channel;
                peer_info.ifidx      = WIFI_IF_STA;
                peer_info.encrypt    = false;
                esp_err_t mod_result = esp_now_mod_peer(&peer_info);
                if (mod_result != ESP_OK) {
                    return mod_result;
                }
            }

            memcpy(updated_peer.mac, mac, 6);
            updated_peer.type         = type;
            updated_peer.channel      = channel;
            updated_peer.last_seen_ms = get_time_ms();

            peers_.erase(it);
            peers_.insert(peers_.begin(), updated_peer);
            return ESP_OK;
        }
    }

    if (peers_.size() >= MAX_PEERS) {
        ESP_LOGW(TAG, "Peer list is full. Removing the oldest peer.");
        const PeerInfo &oldest_peer = peers_.back();
        esp_err_t result            = esp_now_del_peer(oldest_peer.mac);
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove oldest peer to make space: %s",
                     esp_err_to_name(result));
        }
        peers_.pop_back();
    }

    esp_now_peer_info_t peer_info = {};
    memcpy(peer_info.peer_addr, mac, 6);
    peer_info.channel    = channel;
    peer_info.ifidx      = WIFI_IF_STA;
    peer_info.encrypt    = false;
    esp_err_t add_result = esp_now_add_peer(&peer_info);
    if (add_result != ESP_OK) {
        return add_result;
    }

    PeerInfo new_peer;
    memcpy(new_peer.mac, mac, 6);
    new_peer.node_id      = node_id;
    new_peer.type         = type;
    new_peer.channel      = channel;
    new_peer.last_seen_ms = get_time_ms();
    new_peer.paired       = true;
    peers_.insert(peers_.begin(), new_peer);

    ESP_LOGI(TAG, "New peer %02X:%02X:%02X:%02X:%02X:%02X (ID: %" PRIu8 ") added.",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             static_cast<uint8_t>(node_id));
    return ESP_OK;
}

// --- ESP-NOW Callbacks ---
void EspNow::esp_now_recv_cb(const esp_now_recv_info_t *info,
                             const uint8_t *data,
                             int len)
{
    if (info == nullptr || data == nullptr || len <= 0 ||
        len > ESP_NOW_MAX_DATA_LEN) {
        return;
    }

    RxPacket packet;
    memcpy(packet.src_mac, info->src_addr, 6);
    memcpy(packet.data, data, len);
    packet.len          = len;
    packet.rssi         = info->rx_ctrl->rssi;
    packet.timestamp_us = esp_timer_get_time();

    if (xQueueSendFromISR(instance_ptr_->rx_dispatch_queue_, &packet, 0) != pdTRUE) {
        // Log dropped packet
    }
}

void EspNow::esp_now_send_cb(const esp_now_send_info_t *info,
                             esp_now_send_status_t status)
{
    if (info->tx_status == WIFI_SEND_FAIL) {
        if (instance_ptr_ != nullptr &&
            instance_ptr_->tx_manager_task_handle_ != nullptr) {
            xTaskNotify(instance_ptr_->tx_manager_task_handle_, NOTIFY_PHYSICAL_FAIL,
                        eSetBits);
            ESP_EARLY_LOGW(TAG, "ESP-NOW send failed to " MACSTR,
                           MAC2STR(info->des_addr));
        }
    }
}

// --- Tasks ---
void EspNow::rx_dispatch_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    ESP_LOGI(TAG, "rx_dispatch_task started.");

    while (true) {
        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE) {
            if (notifications & NOTIFY_STOP) {
                break;
            }
        }

        if (xQueueReceive(self->rx_dispatch_queue_, &packet, pdMS_TO_TICKS(100)) ==
            pdTRUE) {
            // CRC Validation
            if (packet.len < CRC_SIZE) {
                continue;
            }
            uint8_t received_crc = packet.data[packet.len - 1];
            uint8_t calculated_crc =
                esp_rom_crc8_le(0, packet.data, packet.len - CRC_SIZE);

            if (received_crc != calculated_crc) {
                ESP_LOGW(TAG, "CRC mismatch from " MACSTR, MAC2STR(packet.src_mac));
                continue;
            }

            if (packet.len < sizeof(MessageHeader) + CRC_SIZE) {
                continue;
            }
            const MessageHeader *header =
                reinterpret_cast<const MessageHeader *>(packet.data);

            switch (header->msg_type) {
            case MessageType::PAIR_REQUEST:
            case MessageType::PAIR_RESPONSE:
            case MessageType::HEARTBEAT:
            case MessageType::HEARTBEAT_RESPONSE:
            case MessageType::ACK:
            case MessageType::CHANNEL_SCAN_PROBE:
            case MessageType::CHANNEL_SCAN_RESPONSE:
                if (xQueueSend(self->transport_worker_queue_, &packet, 0) !=
                    pdTRUE) {
                    ESP_LOGW(TAG, "Transport worker queue full.");
                }
                break;
            case MessageType::DATA:
            case MessageType::COMMAND:
                // If the message requires an ACK, save its header before passing to
                // the app
                if (header->requires_ack) {
                    if (xSemaphoreTake(self->ack_mutex_, pdMS_TO_TICKS(10)) ==
                        pdTRUE) {
                        self->last_header_requiring_ack_ = *header;
                        xSemaphoreGive(self->ack_mutex_);
                    }
                }
                if (xQueueSend(self->config_.app_rx_queue, &packet, 0) != pdTRUE) {
                    ESP_LOGW(TAG, "Application RX queue full.");
                }
                break;
            default:
                ESP_LOGW(TAG, "Unknown message type: 0x%02X",
                         static_cast<int>(header->msg_type));
                break;
            }
        }
    }
    ESP_LOGI(TAG, "rx_dispatch_task exiting.");
    vTaskDelete(NULL);
}

void EspNow::send_pair_request()
{
    PairRequest request;
    request.header.msg_type       = MessageType::PAIR_REQUEST;
    request.header.sender_node_id = config_.node_id;
    request.header.sender_type    = config_.node_type;
    request.uptime_ms             = get_time_ms();
    request.heartbeat_interval_ms = config_.heartbeat_interval_ms;

    TxPacket tx_packet;
    const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    memcpy(tx_packet.dest_mac, broadcast_mac, sizeof(broadcast_mac));
    tx_packet.len = sizeof(request);
    memcpy(tx_packet.data, &request, tx_packet.len);
    tx_packet.requires_ack = false;

    if (xQueueSend(tx_queue_, &tx_packet, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue pair request for sending.");
    }
    else {
        ESP_LOGI(TAG, "Queued pairing request.");
        if (tx_manager_task_handle_ != nullptr) {
            xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
        }
    }
}

void EspNow::handle_heartbeat_response(const RxPacket &packet)
{
    const HeartbeatResponse *response =
        reinterpret_cast<const HeartbeatResponse *>(packet.data);
    ESP_LOGI(TAG, "Heartbeat response received from Hub. Wifi Channel: %d",
             response->wifi_channel);
}

void EspNow::handle_scan_probe(const RxPacket &packet)
{
    // Only HUB respond to scan probes
    if (config_.node_type != NodeType::HUB) {
        return;
    }

    const MessageHeader *header =
        reinterpret_cast<const MessageHeader *>(packet.data);

    ESP_LOGI(TAG, "Received scan probe from Node ID %" PRIu8 ". Responding.",
             static_cast<uint8_t>(header->sender_node_id));

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);

    MessageHeader response_header;
    response_header.msg_type       = MessageType::CHANNEL_SCAN_RESPONSE;
    response_header.sender_node_id = config_.node_id;
    response_header.sender_type    = config_.node_type;
    response_header.dest_node_id   = header->sender_node_id;

    tx_packet.len = sizeof(response_header);
    memcpy(tx_packet.data, &response_header, tx_packet.len);
    tx_packet.requires_ack = false;

    if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue scan probe response.");
    }
    else {
        if (tx_manager_task_handle_ != nullptr) {
            xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
        }
    }
}

void EspNow::send_heartbeat()
{
    TxPacket tx_packet;
    if (!find_peer_mac(NodeId::HUB, tx_packet.dest_mac)) {
        ESP_LOGD(TAG, "Hub not found, sending broadcast heartbeat.");
        const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        memcpy(tx_packet.dest_mac, broadcast_mac, sizeof(broadcast_mac));
    }

    HeartbeatMessage heartbeat;
    heartbeat.header.msg_type       = MessageType::HEARTBEAT;
    heartbeat.header.sender_node_id = config_.node_id;
    heartbeat.header.sender_type    = config_.node_type;
    heartbeat.header.dest_node_id   = NodeId::HUB;
    heartbeat.uptime_ms             = get_time_ms();

    tx_packet.len = sizeof(heartbeat);
    memcpy(tx_packet.data, &heartbeat, tx_packet.len);
    tx_packet.requires_ack = false;

    if (xQueueSend(tx_queue_, &tx_packet, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue heartbeat for sending.");
    }
    else {
        if (tx_manager_task_handle_ != nullptr) {
            xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
        }
    }
}

void EspNow::handle_heartbeat(const RxPacket &packet)
{
    if (config_.node_type != NodeType::HUB) {
        return;
    }

    const HeartbeatMessage *msg =
        reinterpret_cast<const HeartbeatMessage *>(packet.data);
    NodeId sender_id = msg->header.sender_node_id;

    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        auto it = std::find_if(peers_.begin(), peers_.end(), [&](const PeerInfo &p) {
            return p.node_id == sender_id;
        });

        if (it != peers_.end()) {
            it->last_seen_ms = get_time_ms();
            ESP_LOGI(TAG,
                     "Heartbeat received from Node ID %" PRIu8
                     ". Updated last_seen.",
                     static_cast<uint8_t>(sender_id));

            // Respond to the heartbeat
            HeartbeatResponse response;
            response.header.msg_type       = MessageType::HEARTBEAT_RESPONSE;
            response.header.sender_node_id = config_.node_id;
            response.header.sender_type    = config_.node_type;
            response.header.dest_node_id   = sender_id;
            response.server_time_ms        = it->last_seen_ms;
            response.wifi_channel          = config_.wifi_channel;

            TxPacket tx_packet;
            memcpy(tx_packet.dest_mac, it->mac, 6);
            tx_packet.len = sizeof(response);
            memcpy(tx_packet.data, &response, tx_packet.len);
            tx_packet.requires_ack = false;
            if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(10)) != pdTRUE) {
                ESP_LOGE(TAG, "Failed to queue heartbeat response.");
            }
            else {
                if (tx_manager_task_handle_ != nullptr) {
                    xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
                }
            }
        }
        else {
            ESP_LOGW(TAG, "Received heartbeat from unknown Node ID: %" PRIu8,
                     static_cast<uint8_t>(sender_id));
        }

        xSemaphoreGive(peers_mutex_);
    }
}

void EspNow::handle_pair_request(const RxPacket &packet)
{
    if (xSemaphoreTake(pairing_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    bool is_active = is_pairing_active_;
    xSemaphoreGive(pairing_mutex_);

    if (config_.node_type != NodeType::HUB || !is_active) {
        return;
    }

    const PairRequest *request = reinterpret_cast<const PairRequest *>(packet.data);

    ESP_LOGI(TAG, "Received pair request from Node ID %" PRIu8,
             static_cast<uint8_t>(request->header.sender_node_id));

    PairResponse response;
    response.header.msg_type       = MessageType::PAIR_RESPONSE;
    response.header.sender_node_id = config_.node_id;
    response.header.sender_type    = config_.node_type;
    response.header.dest_node_id   = request->header.sender_node_id;

    // Check if the request is from another hub and reject it
    if (request->header.sender_type == NodeType::HUB) {
        ESP_LOGW(TAG, "Rejecting pairing request from another Hub");
        response.status = PairStatus::REJECTED_NOT_ALLOWED;

        TxPacket tx_packet;
        memcpy(tx_packet.dest_mac, packet.src_mac, 6);
        tx_packet.len = sizeof(response);
        memcpy(tx_packet.data, &response, tx_packet.len);
        tx_packet.requires_ack = false;
        if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(10)) != pdTRUE) {
            ESP_LOGE(TAG, "Failed to queue pair rejection response.");
        }
        else {
            if (tx_manager_task_handle_ != nullptr) {
                xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
            }
        }
        return;
    }

    // Add the peer and update heartbeat interval under a single mutex lock
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        add_peer_internal(request->header.sender_node_id, packet.src_mac,
                          config_.wifi_channel, request->header.sender_type);

        auto it = std::find_if(peers_.begin(), peers_.end(), [&](const PeerInfo &p) {
            return p.node_id == request->header.sender_node_id;
        });

        if (it != peers_.end()) {
            it->heartbeat_interval_ms = request->heartbeat_interval_ms;
            ESP_LOGI(TAG,
                     "Updated heartbeat interval for Node ID %" PRIu8 " to %" PRIu32
                     " ms.",
                     static_cast<uint8_t>(it->node_id), it->heartbeat_interval_ms);
            save_peers(false);
        }

        // Prepare accepted response
        response.status       = PairStatus::ACCEPTED;
        response.wifi_channel = config_.wifi_channel;

        xSemaphoreGive(peers_mutex_);
    }

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    tx_packet.len = sizeof(response);
    memcpy(tx_packet.data, &response, tx_packet.len);
    tx_packet.requires_ack = false;
    if (xQueueSend(tx_queue_, &tx_packet, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to queue pair acceptance response.");
    }
    else {
        if (tx_manager_task_handle_ != nullptr) {
            xTaskNotify(tx_manager_task_handle_, NOTIFY_DATA, eSetBits);
        }
    }
}

void EspNow::handle_pair_response(const RxPacket &packet)
{
    if (xSemaphoreTake(pairing_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    // Ignore if we are the master or not in pairing mode
    if (config_.node_type == NodeType::HUB || !is_pairing_active_) {
        xSemaphoreGive(pairing_mutex_);
        return;
    }

    const PairResponse *response =
        reinterpret_cast<const PairResponse *>(packet.data);

    if (response->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub (Node ID %" PRIu8 ")",
                 static_cast<uint8_t>(response->header.sender_node_id));

        // Use the channel provided by the hub for communication
        add_peer(response->header.sender_node_id, packet.src_mac,
                 response->wifi_channel, response->header.sender_type);

        // Stop both pairing timers (periodic and timeout)
        if (pairing_timer_handle_ != nullptr) {
            xTimerDelete(pairing_timer_handle_, portMAX_DELAY);
            pairing_timer_handle_ = nullptr;
        }
        if (pairing_timeout_timer_handle_ != nullptr) {
            xTimerDelete(pairing_timeout_timer_handle_, portMAX_DELAY);
            pairing_timeout_timer_handle_ = nullptr;
        }
        is_pairing_active_ = false;
        ESP_LOGI(TAG, "Pairing successful.");
    }
    else {
        ESP_LOGW(TAG, "Pairing rejected by Hub.");
    }

    xSemaphoreGive(pairing_mutex_);
}

void EspNow::transport_worker_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    RxPacket packet;

    ESP_LOGI(TAG, "transport_worker_task started.");

    while (true) {
        uint32_t notifications = 0;
        if (xTaskNotifyWait(0, NOTIFY_STOP, &notifications, 0) == pdTRUE) {
            if (notifications & NOTIFY_STOP) {
                break;
            }
        }

        if (xQueueReceive(self->transport_worker_queue_, &packet,
                          pdMS_TO_TICKS(100)) == pdTRUE) {
            if (packet.len < sizeof(MessageHeader) + CRC_SIZE) {
                continue;
            }
            const MessageHeader *header =
                reinterpret_cast<const MessageHeader *>(packet.data);

            // Any valid protocol message proves the link is alive and we are on the
            // correct channel
            xTaskNotify(self->tx_manager_task_handle_, NOTIFY_LINK_ALIVE, eSetBits);

            switch (header->msg_type) {
            case MessageType::PAIR_REQUEST:
                self->handle_pair_request(packet);
                break;
            case MessageType::PAIR_RESPONSE:
                self->handle_pair_response(packet);
                break;
            case MessageType::HEARTBEAT:
                self->handle_heartbeat(packet);
                break;
            case MessageType::HEARTBEAT_RESPONSE:
                self->handle_heartbeat_response(packet);
                break;
            case MessageType::ACK:
            {
                // Notify the TX manager that a logical ACK was received.
                xTaskNotify(self->tx_manager_task_handle_, NOTIFY_LOGICAL_ACK,
                            eSetBits);
                break;
            }
            case MessageType::CHANNEL_SCAN_PROBE:
                self->handle_scan_probe(packet);
                break;
            case MessageType::CHANNEL_SCAN_RESPONSE:
            {
                const MessageHeader *header =
                    reinterpret_cast<const MessageHeader *>(packet.data);
                ESP_LOGI(TAG, "Hub responded to scan. Updating peer info.");
                // We don't have the channel in the response yet, let's assume it's
                // the current channel This part needs the channel to be added to
                // CHANNEL_SCAN_RESPONSE for full correctness. For now, we assume the
                // scan loop channel is correct.
                uint8_t current_channel;
                esp_wifi_get_channel(&current_channel, nullptr);
                self->update_wifi_channel(current_channel);
                self->add_peer(header->sender_node_id, packet.src_mac,
                               current_channel, header->sender_type);

                // Notify the TX manager that the Hub was found.
                xTaskNotify(self->tx_manager_task_handle_, NOTIFY_HUB_FOUND,
                            eSetBits);
                break;
            }
            default:
                break;
            }
        }
    }
    ESP_LOGI(TAG, "transport_worker_task exiting.");
    vTaskDelete(NULL);
}

void EspNow::pairing_timer_cb(TimerHandle_t xTimer)
{
    EspNow *self = static_cast<EspNow *>(pvTimerGetTimerID(xTimer));
    if (self != nullptr && self->tx_manager_task_handle_ != nullptr) {
        xTaskNotify(self->tx_manager_task_handle_, NOTIFY_PAIRING_TIMEOUT, eSetBits);
    }
}

void EspNow::periodic_pairing_cb(TimerHandle_t xTimer)
{
    EspNow *self = static_cast<EspNow *>(pvTimerGetTimerID(xTimer));
    if (self != nullptr && self->tx_manager_task_handle_ != nullptr) {
        // Notify the TX manager task to send a pairing request
        xTaskNotify(self->tx_manager_task_handle_, NOTIFY_PAIRING, eSetBits);
    }
}

void EspNow::periodic_heartbeat_cb(TimerHandle_t xTimer)
{
    EspNow *self = static_cast<EspNow *>(pvTimerGetTimerID(xTimer));
    if (self != nullptr && self->tx_manager_task_handle_ != nullptr) {
        // Notify the TX manager task to send a heartbeat
        xTaskNotify(self->tx_manager_task_handle_, NOTIFY_HEARTBEAT, eSetBits);
    }
}

uint32_t EspNow::get_time_ms() const
{
    return esp_timer_get_time() / 1000;
}

EspNowStorage::Peer EspNow::info_to_storage(const PeerInfo &info)
{
    EspNowStorage::Peer storage;
    memcpy(storage.mac, info.mac, 6);
    storage.type                  = info.type;
    storage.node_id               = info.node_id;
    storage.channel               = info.channel;
    storage.paired                = info.paired;
    storage.heartbeat_interval_ms = info.heartbeat_interval_ms;
    return storage;
}

EspNow::PeerInfo EspNow::storage_to_info(const EspNowStorage::Peer &storage)
{
    PeerInfo info;
    memcpy(info.mac, storage.mac, 6);
    info.type                  = storage.type;
    info.node_id               = storage.node_id;
    info.channel               = storage.channel;
    info.last_seen_ms          = 0; // Relative to current boot
    info.paired                = storage.paired;
    info.heartbeat_interval_ms = storage.heartbeat_interval_ms;
    return info;
}

void EspNow::save_peers(bool force_nvs_commit)
{
    std::vector<EspNowStorage::Peer> storage_peers;
    for (const auto &p : peers_) {
        storage_peers.push_back(info_to_storage(p));
    }
    storage_.save(config_.wifi_channel, storage_peers, force_nvs_commit);
}

void EspNow::update_wifi_channel(uint8_t channel)
{
    if (xSemaphoreTake(peers_mutex_, portMAX_DELAY) == pdTRUE) {
        if (config_.wifi_channel != channel) {
            config_.wifi_channel = channel;
            ESP_LOGI(TAG, "Wi-Fi channel updated to %d", channel);

            // Update broadcast peer channel
            esp_now_peer_info_t broadcast_peer = {};
            const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
            memcpy(broadcast_peer.peer_addr, broadcast_mac, 6);
            broadcast_peer.channel = channel;
            broadcast_peer.ifidx   = WIFI_IF_STA;
            broadcast_peer.encrypt = false;

            esp_err_t err = esp_now_mod_peer(&broadcast_peer);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update broadcast peer channel: %s",
                         esp_err_to_name(err));
            }
            save_peers(true);
        }
        xSemaphoreGive(peers_mutex_);
    }
}

void EspNow::tx_manager_task(void *arg)
{
    EspNow *self = static_cast<EspNow *>(arg);
    TxPacket packet_to_send;
    TxState current_state            = TxState::IDLE;
    static uint16_t sequence_counter = 0;
    std::optional<PendingAck> pending_ack_msg;
    uint8_t phy_send_fail_count = 0; // Fails for the current message waiting for ACK
    uint8_t phy_consecutive_fail_count =
        0; // Total consecutive fails regardless of state

    self->ack_timeout_timer_handle_ = xTimerCreate(
        "ack_timeout", pdMS_TO_TICKS(LOGICAL_ACK_TIMEOUT_MS), pdFALSE,
        self->tx_manager_task_handle_, [](TimerHandle_t xTimer) {
            xTaskNotify(static_cast<TaskHandle_t>(pvTimerGetTimerID(xTimer)),
                        NOTIFY_ACK_TIMEOUT, eSetBits);
        });

    if (self->ack_timeout_timer_handle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create ack_timeout_timer.");
        vTaskDelete(nullptr); // Abort task
    }

    ESP_LOGI(TAG, "tx_manager_task started.");

    while (true) {
        uint32_t notifications = 0;

        switch (current_state) {
        case TxState::IDLE:
        {
            // First, check if there's anything already in the queue to process
            if (xQueueReceive(self->tx_queue_, &packet_to_send, 0) == pdTRUE) {
                current_state = TxState::SENDING;
                break;
            }

            // Wait for any interesting notification
            if (xTaskNotifyWait(0,
                                NOTIFY_DATA | NOTIFY_HEARTBEAT | NOTIFY_PAIRING |
                                    NOTIFY_PAIRING_TIMEOUT | NOTIFY_STOP |
                                    NOTIFY_PHYSICAL_FAIL | NOTIFY_LINK_ALIVE,
                                &notifications, portMAX_DELAY) == pdTRUE) {
                if (notifications & NOTIFY_STOP) {
                    goto exit;
                }

                if (notifications & NOTIFY_LINK_ALIVE) {
                    phy_consecutive_fail_count = 0;
                    phy_send_fail_count        = 0;
                }

                if (notifications & NOTIFY_PHYSICAL_FAIL) {
                    phy_consecutive_fail_count++;
                    ESP_LOGW(
                        TAG,
                        "Physical failure detected in IDLE. Consecutive count: %d",
                        phy_consecutive_fail_count);
                    if (phy_consecutive_fail_count >= 3) {
                        ESP_LOGE(TAG, "Consecutive physical failures reached limit. "
                                      "Starting SCANNING.");
                        phy_consecutive_fail_count = 0;
                        current_state              = TxState::SCANNING;
                        break;
                    }
                }

                if (notifications & NOTIFY_HEARTBEAT) {
                    self->send_heartbeat();
                }

                if (notifications & NOTIFY_PAIRING) {
                    self->send_pair_request();
                }

                if (notifications & NOTIFY_PAIRING_TIMEOUT) {
                    if (xSemaphoreTake(self->pairing_mutex_, pdMS_TO_TICKS(100)) ==
                        pdTRUE) {
                        if (self->config_.node_type == NodeType::HUB) {
                            self->is_pairing_active_ = false;
                            ESP_LOGI(TAG,
                                     "Pairing timeout reached. Pairing stopped.");
                        }
                        else {
                            ESP_LOGW(TAG, "Pairing attempt timed out.");
                            if (self->pairing_timer_handle_ != nullptr) {
                                xTimerDelete(self->pairing_timer_handle_,
                                             portMAX_DELAY);
                                self->pairing_timer_handle_ = nullptr;
                            }
                            self->is_pairing_active_ = false;
                        }
                        xSemaphoreGive(self->pairing_mutex_);
                    }
                }

                // If NOTIFY_DATA was received, the next loop iteration will pick it
                // up via xQueueReceive above.
            }
            break;
        }

        case TxState::SENDING:
        {
            MessageHeader *header =
                reinterpret_cast<MessageHeader *>(packet_to_send.data);
            header->sequence_number = sequence_counter++;

            esp_err_t send_result = self->send_packet(
                packet_to_send.dest_mac, packet_to_send.data, packet_to_send.len);

            if (send_result == ESP_OK && packet_to_send.requires_ack) {
                pending_ack_msg =
                    PendingAck{.sequence_number = header->sequence_number,
                               .timestamp_ms    = self->get_time_ms(),
                               .retries_left    = MAX_LOGICAL_RETRIES,
                               .packet          = packet_to_send};

                xTimerStart(self->ack_timeout_timer_handle_, 0);
                current_state = TxState::WAITING_FOR_ACK;
            }
            else {
                current_state = TxState::IDLE;
            }
            break;
        }

        case TxState::WAITING_FOR_ACK:
        {
            if (xTaskNotifyWait(0,
                                NOTIFY_LOGICAL_ACK | NOTIFY_PHYSICAL_FAIL |
                                    NOTIFY_ACK_TIMEOUT | NOTIFY_PAIRING_TIMEOUT |
                                    NOTIFY_STOP | NOTIFY_LINK_ALIVE,
                                &notifications, portMAX_DELAY) == pdPASS) {
                if (notifications & NOTIFY_STOP) {
                    goto exit;
                }

                if (notifications & NOTIFY_LINK_ALIVE) {
                    phy_consecutive_fail_count = 0;
                    phy_send_fail_count        = 0;
                }

                if (notifications & NOTIFY_LOGICAL_ACK) {
                    ESP_LOGD(TAG, "ACK received. Returning to IDLE.");
                    phy_send_fail_count        = 0;
                    phy_consecutive_fail_count = 0;
                    pending_ack_msg.reset();
                    xTimerStop(self->ack_timeout_timer_handle_, 0);
                    current_state = TxState::IDLE;
                }
                else if (notifications & NOTIFY_PHYSICAL_FAIL) {
                    phy_send_fail_count++;
                    phy_consecutive_fail_count++;
                    if (pending_ack_msg) {
                        ESP_LOGW(TAG,
                                 "Physical failure for seq %u. Current msg fails: "
                                 "%d, Consecutive: %d",
                                 pending_ack_msg->sequence_number,
                                 phy_send_fail_count, phy_consecutive_fail_count);

                        if (phy_send_fail_count >= MAX_LOGICAL_RETRIES ||
                            phy_consecutive_fail_count >= 3) {
                            ESP_LOGE(TAG, "Triggering SCANNING state due to "
                                          "physical failures.");
                            phy_send_fail_count        = 0;
                            phy_consecutive_fail_count = 0;
                            pending_ack_msg.reset();
                            xTimerStop(self->ack_timeout_timer_handle_, 0);
                            current_state = TxState::SCANNING;
                        }
                    }
                    else {
                        ESP_LOGW(TAG,
                                 "Physical send failed for non-ACK packet. "
                                 "Consecutive: %d",
                                 phy_consecutive_fail_count);
                        if (phy_consecutive_fail_count >= 3) {
                            ESP_LOGE(TAG, "Consecutive physical failures reached "
                                          "limit. Starting SCANNING.");
                            phy_consecutive_fail_count = 0;
                            phy_send_fail_count        = 0;
                            current_state              = TxState::SCANNING;
                        }
                        else {
                            phy_send_fail_count = 0;
                        }
                    }
                }
                else if (notifications & NOTIFY_ACK_TIMEOUT) {
                    current_state = TxState::RETRYING;
                }

                if (notifications & NOTIFY_PAIRING_TIMEOUT) {
                    if (xSemaphoreTake(self->pairing_mutex_, pdMS_TO_TICKS(100)) ==
                        pdTRUE) {
                        if (self->config_.node_type == NodeType::HUB) {
                            self->is_pairing_active_ = false;
                            ESP_LOGI(TAG,
                                     "Pairing timeout reached. Pairing stopped.");
                        }
                        else {
                            ESP_LOGW(TAG, "Pairing attempt timed out.");
                            if (self->pairing_timer_handle_ != nullptr) {
                                xTimerDelete(self->pairing_timer_handle_,
                                             portMAX_DELAY);
                                self->pairing_timer_handle_ = nullptr;
                            }
                            self->is_pairing_active_ = false;
                        }
                        xSemaphoreGive(self->pairing_mutex_);
                    }
                }
            }
            break;
        }

        case TxState::RETRYING:
        {
            if (pending_ack_msg && pending_ack_msg->retries_left > 0) {
                pending_ack_msg->retries_left--;
                ESP_LOGW(TAG, "ACK timeout for seq %u. Retrying... (%d left)",
                         pending_ack_msg->sequence_number,
                         pending_ack_msg->retries_left);
                self->send_packet(pending_ack_msg->packet.dest_mac,
                                  pending_ack_msg->packet.data,
                                  pending_ack_msg->packet.len);
                xTimerStart(self->ack_timeout_timer_handle_, 0);
                current_state = TxState::WAITING_FOR_ACK;
            }
            else {
                ESP_LOGE(TAG, "Failed to send packet with seq %u after max retries.",
                         pending_ack_msg->sequence_number);
                pending_ack_msg.reset();
                current_state = TxState::IDLE;
            }
            break;
        }

        case TxState::SCANNING:
        {
            ESP_LOGW(TAG, "Starting channel scan to find Hub.");
            bool hub_found = false;
            if (self->heartbeat_timer_handle_ != nullptr) {
                xTimerStop(self->heartbeat_timer_handle_, portMAX_DELAY);
            }

            uint8_t current_channel;
            esp_wifi_get_channel(&current_channel, nullptr);
            if (current_channel < 1 || current_channel > 13) {
                current_channel = 1;
            }
            uint32_t scan_start_time = self->get_time_ms();

            for (uint8_t offset = 0; offset < 13 && !hub_found; ++offset) {
                uint8_t channel = ((current_channel - 1 + offset) % 13) + 1;

                esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

                TxPacket probe_packet;
                const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                memcpy(probe_packet.dest_mac, broadcast_mac, sizeof(broadcast_mac));

                MessageHeader probe_header;
                probe_header.msg_type       = MessageType::CHANNEL_SCAN_PROBE;
                probe_header.sender_node_id = self->config_.node_id;
                probe_header.sender_type    = self->config_.node_type;
                probe_header.dest_node_id   = NodeId::HUB;

                probe_packet.len = sizeof(probe_header);
                memcpy(probe_packet.data, &probe_header, probe_packet.len);
                probe_packet.requires_ack = false;

                for (uint8_t attempt = 0;
                     attempt < SCAN_CHANNEL_ATTEMPTS && !hub_found; attempt++) {
                    self->send_packet(probe_packet.dest_mac, probe_packet.data,
                                      probe_packet.len);

                    if (xTaskNotifyWait(
                            0, NOTIFY_HUB_FOUND | NOTIFY_LINK_ALIVE, &notifications,
                            pdMS_TO_TICKS(SCAN_CHANNEL_TIMEOUT_MS)) == pdPASS) {
                        if (notifications & (NOTIFY_HUB_FOUND | NOTIFY_LINK_ALIVE)) {
                            ESP_LOGI(TAG, "Hub found on channel %d. Re-syncing.",
                                     channel);
                            hub_found                  = true;
                            phy_consecutive_fail_count = 0;
                            break;
                        }
                    }
                    if (attempt < SCAN_CHANNEL_ATTEMPTS - 1) {
                        vTaskDelay(pdMS_TO_TICKS(5));
                    }
                }
                if (offset < 12 && !hub_found) {
                    vTaskDelay(pdMS_TO_TICKS(2));
                }
                uint32_t current_time = self->get_time_ms();
                if (current_time - scan_start_time > MAX_SCAN_TIME_MS) {
                    break;
                }
            }

            if (!hub_found) {
                ESP_LOGE(TAG, "Hub not found after full scan.");
                esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
            }

            if (self->heartbeat_timer_handle_ != nullptr) {
                xTimerStart(self->heartbeat_timer_handle_, 0);
            }
            current_state = TxState::IDLE;
            break;
        }

        default:
            current_state = TxState::IDLE;
            break;
        }
    }

exit:
    ESP_LOGI(TAG, "tx_manager_task exiting.");
    vTaskDelete(NULL);
}
