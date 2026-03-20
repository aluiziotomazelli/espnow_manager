#include <cstring>

#include "esp_log.h"

#include "message_router.hpp"

static const char *TAG = "MessageRouter";

MessageRouter::MessageRouter(
    IDiscoveryManager &discovery_manager,
    ITxManager &tx_manager,
    IHeartbeatManager &heartbeat_manager,
    IPairingManager &pairing_manager,
    IMessageCodec &message_codec)
    : discovery_manager_(discovery_manager)
    , tx_manager_(tx_manager)
    , heartbeat_manager_(heartbeat_manager)
    , pairing_manager_(pairing_manager)
    , message_codec_(message_codec)
{
}

void MessageRouter::handle_packet(const RxPacket &packet)
{
    auto header_opt = message_codec_.decode_header(packet.data, packet.len);
    if (!header_opt)
        return;
    const MessageHeader &header = header_opt.value();

    tx_manager_.notify_link_alive();
    // TODO: update last seen here or on manager rx_dispatch_task

    switch (header.msg_type) {
    case MessageType::PAIR_REQUEST:
        if (packet.len < sizeof(PairRequest)) {
            ESP_LOGW(TAG, "Malformed PAIR_REQUEST: len %d < %d", (int)packet.len, (int)sizeof(PairRequest));
            return;
        }
        pairing_manager_.handle_request(packet);
        break;
    case MessageType::PAIR_RESPONSE:
        if (packet.len < sizeof(PairResponse)) {
            ESP_LOGW(TAG, "Malformed PAIR_RESPONSE: len %d < %d", (int)packet.len, (int)sizeof(PairResponse));
            return;
        }
        pairing_manager_.handle_response(packet);
        break;
    case MessageType::HEARTBEAT:
        heartbeat_manager_.handle_request(packet);
        break;
    case MessageType::HEARTBEAT_RESPONSE:
    {
        if (packet.len < sizeof(HeartbeatResponse)) {
            ESP_LOGW(TAG, "Malformed HEARTBEAT_RESPONSE: len %d < %d", (int)packet.len, (int)sizeof(HeartbeatResponse));
            return;
        }
        // MessageRouter just passes header.sender_node_id, resp->wifi_channel is ignored
        heartbeat_manager_.handle_response(header.sender_node_id);
        break;
    }
    case MessageType::ACK:
        tx_manager_.notify_logical_ack();
        break;
    case MessageType::CHANNEL_SCAN_PROBE:
        discovery_manager_.handle_probe(packet);
        break;
    case MessageType::CHANNEL_SCAN_RESPONSE:
    {
        // Hub found response, notify link alive to resume TX
        tx_manager_.notify_link_alive();
        break;
    }
    default:
        break;
    }
}
