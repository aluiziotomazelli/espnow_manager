#include <cstring>

#include "esp_log.h"

#include "channel_scanner.hpp"
#include "protocol_types.hpp"

static const char *TAG = "ChannelScanner";

ChannelScanner::ChannelScanner(
    IWiFiHAL &wifi_hal,
    IMessageCodec &message_codec,
    IFreeRTOSHAL &freertos_hal,
    NodeId my_node_id,
    NodeType my_node_type)
    : wifi_hal_(wifi_hal)
    , message_codec_(message_codec)
    , freertos_hal_(freertos_hal)
    , my_node_id_(my_node_id)
    , my_node_type_(my_node_type)
{
}

void ChannelScanner::update_node_info(NodeId id, NodeType type)
{
    my_node_id_ = id;
    my_node_type_ = type;
}

IChannelScanner::ScanResult ChannelScanner::scan(uint8_t start_channel)
{
    ESP_LOGI(TAG, "Starting channel scan to find Hub.");

    bool hub_found = false;
    uint8_t current_channel = start_channel;

    if (current_channel < 1 || current_channel > 13) {
        current_channel = 1;
    }

    // In total loop for scan, we will start from actual channel (most likely to be the correct one)
    // and make SCAN_CHANNEL_ATTEMPTS on each of 13 wifi channels
    for (uint8_t offset = 0; offset < 13 && !hub_found; ++offset) {
        uint8_t channel = ((current_channel - 1 + offset) % 13) + 1;
        wifi_hal_.wifi_set_channel(channel);

        // empty initializer to avoid memory garbage on unused fields
        MessageHeader probe_header = {};
        probe_header.msg_type = MessageType::CHANNEL_SCAN_PROBE;
        probe_header.sender_node_id = my_node_id_;
        probe_header.sender_type = my_node_type_;
        probe_header.dest_node_id = ReservedIds::HUB; // Destination is always hub

        auto encoded = message_codec_.encode(probe_header, nullptr, 0);
        if (encoded.empty())
            continue;

        const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        // Loop to send probe * SCAN_CHANNEL_ATTEMPTS until the hub is not found
        for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS && !hub_found; attempt++) {
            wifi_hal_.hal_esp_now_send(broadcast_mac, encoded.data(), encoded.size());

            // Wait for hub to respond
            uint32_t notifications = 0;
            if (freertos_hal_.task_notify_wait(
                    0, NOTIFY_HUB_FOUND | NOTIFY_LINK_ALIVE, &notifications, SCAN_CHANNEL_TIMEOUT_MS) == pdPASS) {
                if (notifications & (NOTIFY_HUB_FOUND | NOTIFY_LINK_ALIVE)) {
                    ESP_LOGI(TAG, "Hub found on channel %d.", channel);
                    hub_found = true;
                    current_channel = channel; // update only if hub is found
                    break;
                }
            }
        }
    }

    return {current_channel, hub_found};
}
