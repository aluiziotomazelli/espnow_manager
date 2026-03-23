// src/discovery_manager.cpp
#include <cstring>

#include "esp_log.h"

#include "discovery_manager.hpp"
#include "protocol_types.hpp"

static const char *TAG = "DiscoveryMgr";

DiscoveryManager::DiscoveryManager(IWiFiHAL &wifi_hal, IMessageCodec &message_codec, IFreeRTOSHAL &freertos_hal)
    : hal_wifi_(wifi_hal)
    , message_codec_(message_codec)
    , hal_freertos_(freertos_hal)
{
}

esp_err_t DiscoveryManager::init(NodeId id, NodeType type, ITxManager *tx_mgr, IChannelObserver *observer)
{
    my_node_id_ = id;
    my_node_type_ = type;

    if (type == ReservedTypes::HUB) {
        if (tx_mgr == nullptr) {
            ESP_LOGE(TAG, "TxManager is required for Hub type.");
            return ESP_ERR_INVALID_ARG;
        }
        tx_mgr_ = tx_mgr;
        hub_ready_ = true;
    }
    else {
        if (observer == nullptr) {
            ESP_LOGE(TAG, "ChannelObserver is required for non-Hub type.");
            return ESP_ERR_INVALID_ARG;
        }
        observer_ = observer;
        node_ready_ = true;
    }

    return ESP_OK;
}

IDiscoveryManager::ScanResult DiscoveryManager::scan()
{
    if (!node_ready_) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before scan().");
        return {current_channel_, false};
    }

    // Signal to EspNowManager that scan is starting
    observer_->on_scan_started_cb();

    ESP_LOGI(TAG, "Starting channel scan to find Hub.");
    IDiscoveryManager::ScanResult result = {current_channel_, false};

    // In total loop for scan, we will start from actual channel (most likely to be the correct one)
    // and make SCAN_CHANNEL_ATTEMPTS on each of 13 wifi channels
    for (uint8_t offset = 0; offset < 13 && !result.hub_found; ++offset) {
        uint8_t channel = ((current_channel_ - 1 + offset) % 13) + 1;

        // Temporarily set channel to send probe; final channel update
        // is handled by EspNowManager via on_channel_found_cb() callback
        esp_err_t err = hal_wifi_.wifi_set_channel(channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set WiFi channel to %d: %s", channel, esp_err_to_name(err));
            continue;
        }
        err = modify_broadcast_peer(channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to modify broadcast peer for channel %d: %s", channel, esp_err_to_name(err));
            continue;
        }

        // empty initializer to avoid memory garbage on unused fields
        MessageHeader probe_header = {};
        probe_header.msg_type = MessageType::CHANNEL_SCAN_PROBE;
        probe_header.sender_node_id = my_node_id_;
        probe_header.sender_type = my_node_type_;
        probe_header.dest_node_id = ReservedIds::HUB; // Destination is always hub

        uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
        size_t encoded_len = message_codec_.encode(probe_header, nullptr, 0, buffer, sizeof(buffer));
        if (encoded_len == 0) {
            continue;
        }

        // Loop to send probe * SCAN_CHANNEL_ATTEMPTS until the hub is not found
        for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS && !result.hub_found; attempt++) {
            hal_wifi_.hal_esp_now_send(BROADCAST_MAC, buffer, encoded_len);
            ESP_LOGV(TAG, "Probe sent on channel: %d", channel);

            // Wait for hub to respond
            uint32_t notifications = 0;
            if (hal_freertos_.task_notify_wait(
                    0, NOTIFY_LINK_ALIVE, &notifications, pdMS_TO_TICKS(SCAN_CHANNEL_TIMEOUT_MS)) == pdPASS) {
                if (notifications & NOTIFY_LINK_ALIVE) {
                    ESP_LOGI(TAG, "Hub found on channel: %d", channel);
                    current_channel_ = channel; // Update current channel to the one where hub is found
                    observer_->on_channel_found_cb(channel);
                    result.channel = channel;
                    result.hub_found = true;
                    break;
                }
            }
        }
    }

    // Notify scan failure to trigger potential fallback actions
    if (!result.hub_found) {
        ESP_LOGW(TAG, "Hub not found after scanning all channels.");
        observer_->on_scan_failed_cb();
    }

    return result;
}

void DiscoveryManager::handle_probe(const DecodedPacket &decoded)
{
    if (!hub_ready_) {
        ESP_LOGE(TAG, "DiscoveryManager not initialized properly. Call init() before handle_probe().");
        return;
    }
    ESP_LOGD(
        TAG,
        "Received channel scan probe from node_id=%d, sender_type=%d",
        decoded.header.sender_node_id,
        decoded.header.sender_type);
    // Build CHANNEL_SCAN_RESPONSE header
    MessageHeader resp{};
    resp.msg_type = MessageType::CHANNEL_SCAN_RESPONSE;
    resp.sender_node_id = my_node_id_;
    resp.sender_type = my_node_type_;
    resp.dest_node_id = decoded.header.sender_node_id;

    uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
    size_t len = message_codec_.encode(resp, nullptr, 0, buffer, sizeof(buffer));
    if (len == 0) {
        return;
    }

    // Send directly via WiFi HAL — the probing node may not yet be a registered
    // ESP-NOW peer, so TxManager cannot be used here.
    esp_err_t err = hal_wifi_.hal_esp_now_send(BROADCAST_MAC, buffer, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send channel scan response: %s", esp_err_to_name(err));
    }
}

void DiscoveryManager::set_channel(uint8_t channel)
{
    current_channel_ = channel;
}

esp_err_t DiscoveryManager::modify_broadcast_peer(const uint8_t &channel)
{
    esp_now_peer_info_t broadcast = {};
    memcpy(broadcast.peer_addr, BROADCAST_MAC, 6);
    broadcast.channel = channel;
    broadcast.ifidx = WIFI_IF_STA;
    broadcast.encrypt = false;
    return hal_wifi_.hal_esp_now_mod_peer(&broadcast);
}