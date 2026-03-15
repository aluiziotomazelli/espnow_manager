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

esp_err_t DiscoveryManager::init(NodeId id, NodeType type, ITxManager &tx_mgr, IChannelObserver *observer)
{
    my_node_id_ = id;
    my_node_type_ = type;
    tx_mgr_ = &tx_mgr;
    observer_ = observer;
    return ESP_OK;
}

IDiscoveryManager::ScanResult DiscoveryManager::scan(uint8_t start_channel)
{
    ESP_LOGI(TAG, "Starting channel scan to find Hub.");

    IDiscoveryManager::ScanResult result = {start_channel, false};
    uint8_t current_channel = start_channel;

    if (current_channel < 1 || current_channel > 13) {
        current_channel = 1;
    }

    // In total loop for scan, we will start from actual channel (most likely to be the correct one)
    // and make SCAN_CHANNEL_ATTEMPTS on each of 13 wifi channels
    for (uint8_t offset = 0; offset < 13 && !result.hub_found; ++offset) {
        uint8_t channel = ((current_channel - 1 + offset) % 13) + 1;

        // Temporarily set channel to send probe; final channel update
        // is handled by EspNowManager via on_channel_found() callback
        hal_wifi_.wifi_set_channel(channel);

        // empty initializer to avoid memory garbage on unused fields
        MessageHeader probe_header = {};
        probe_header.msg_type = MessageType::CHANNEL_SCAN_PROBE;
        probe_header.sender_node_id = my_node_id_;
        probe_header.sender_type = my_node_type_;
        probe_header.dest_node_id = ReservedIds::HUB; // Destination is always hub

        uint8_t buffer[ESP_NOW_MAX_DATA_LEN];
        size_t encoded_len = message_codec_.encode(probe_header, nullptr, 0, buffer, sizeof(buffer));
        if (encoded_len == 0)
            continue;

        // Loop to send probe * SCAN_CHANNEL_ATTEMPTS until the hub is not found
        for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS && !result.hub_found; attempt++) {
            hal_wifi_.hal_esp_now_send(BROADCAST_MAC, buffer, encoded_len);

            // Wait for hub to respond
            uint32_t notifications = 0;
            if (hal_freertos_.task_notify_wait(
                    0, NOTIFY_LINK_ALIVE, &notifications, pdMS_TO_TICKS(SCAN_CHANNEL_TIMEOUT_MS)) == pdPASS) {
                if (notifications & NOTIFY_LINK_ALIVE) {
                    ESP_LOGI(TAG, "Hub found on channel %d.", channel);
                    if (observer_) { // programmatic error if init() wasn't called
                        observer_->on_channel_found(channel);
                    }
                    result.channel = channel;
                    result.hub_found = true;
                    break;
                }
            }
        }
    }

    return result;
}

void DiscoveryManager::handle_probe(const RxPacket &packet)
{
    if (tx_mgr_ == nullptr || my_node_type_ != ReservedTypes::HUB) {
        return;
    }

    auto header_opt = message_codec_.decode_header(packet.data, packet.len);
    if (!header_opt)
        return;

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    MessageHeader resp;
    resp.msg_type = MessageType::CHANNEL_SCAN_RESPONSE;
    resp.sender_node_id = my_node_id_;
    resp.sender_type = my_node_type_;
    resp.dest_node_id = header_opt->sender_node_id;
    resp.sequence_number = 0;
    resp.requires_ack = false;
    resp.timestamp_ms = 0; // Fixed timestamp for control messages if not used

    tx_packet.len = message_codec_.encode(resp, nullptr, 0, tx_packet.data, sizeof(tx_packet.data));
    if (tx_packet.len == 0)
        return;

    tx_packet.requires_ack = false;
    tx_mgr_->queue_packet(tx_packet);
}
