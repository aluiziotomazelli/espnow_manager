#include "channel_scanner.hpp"
#include "protocol_types.hpp"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "ChannelScanner";

RealChannelScanner::RealChannelScanner(IWiFiHAL &wifi_hal, IMessageCodec &message_codec, NodeId my_node_id, NodeType my_node_type)
    : wifi_hal_(wifi_hal)
    , message_codec_(message_codec)
    , my_node_id_(my_node_id)
    , my_node_type_(my_node_type)
{
}

void RealChannelScanner::update_node_info(NodeId id, NodeType type)
{
    my_node_id_ = id;
    my_node_type_ = type;
}

IChannelScanner::ScanResult RealChannelScanner::scan(uint8_t start_channel)
{
    ESP_LOGI(TAG, "Starting channel scan to find Hub.");
    bool hub_found = false;
    uint8_t current_channel = start_channel;
    if (current_channel < 1 || current_channel > 13) {
        current_channel = 1;
    }

    // Note: get_time_ms logic should be handled by HAL or passed?
    // Let's assume HAL provides time or we just use a loop count.
    // Original used get_time_ms.

    // For simplicity, let's keep the loop.
    for (uint8_t offset = 0; offset < 13 && !hub_found; ++offset) {
        uint8_t channel = ((current_channel - 1 + offset) % 13) + 1;
        wifi_hal_.set_channel(channel);

        MessageHeader probe_header;
        probe_header.msg_type       = MessageType::CHANNEL_SCAN_PROBE;
        probe_header.sender_node_id = my_node_id_;
        probe_header.sender_type    = my_node_type_;
        probe_header.dest_node_id   = ReservedIds::HUB;
        probe_header.sequence_number = 0;
        probe_header.timestamp_ms    = 0; // Not critical for probe

        auto encoded = message_codec_.encode(probe_header, nullptr, 0);
        if (encoded.empty()) continue;

        const uint8_t broadcast_mac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

        for (uint8_t attempt = 0; attempt < SCAN_CHANNEL_ATTEMPTS && !hub_found; attempt++) {
            wifi_hal_.send_packet(broadcast_mac, encoded.data(), encoded.size());

            // Notification bits: NOTIFY_HUB_FOUND | NOTIFY_LINK_ALIVE
            // Let's use 0x04 | 0x200 = 0x204
            if (wifi_hal_.wait_for_event(0x204, SCAN_CHANNEL_TIMEOUT_MS)) {
                ESP_LOGI(TAG, "Hub found on channel %d.", channel);
                hub_found = true;
                current_channel = channel;
                break;
            }
        }
    }

    return {current_channel, hub_found};
}
