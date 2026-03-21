#include <cstring>

#include "esp_log.h"

#include "message_router.hpp"

static const char *TAG = "MessageRouter";

MessageRouter::MessageRouter(
    IDiscoveryManager &discovery_manager,
    ITxManager &tx_manager,
    IHeartbeatManager &heartbeat_manager,
    IPairingManager &pairing_manager)
    : discovery_manager_(discovery_manager)
    , tx_manager_(tx_manager)
    , heartbeat_manager_(heartbeat_manager)
    , pairing_manager_(pairing_manager)

{
}

void MessageRouter::handle_packet(const DecodedPacket &decoded)
{
    // TODO: update last seen here or on manager rx_dispatch_task
    tx_manager_.notify_link_alive();

    switch (decoded.header.msg_type) {
    case MessageType::PAIR_REQUEST:
        if (decoded.raw.len < sizeof(PairRequest)) {
            ESP_LOGW(TAG, "Malformed PAIR_REQUEST: len %d < %d", (int)decoded.raw.len, (int)sizeof(PairRequest));
            return;
        }
        pairing_manager_.handle_request(decoded);
        break;
    case MessageType::PAIR_RESPONSE:
        if (decoded.raw.len < sizeof(PairResponse)) {
            ESP_LOGW(TAG, "Malformed PAIR_RESPONSE: len %d < %d", (int)decoded.raw.len, (int)sizeof(PairResponse));
            return;
        }
        pairing_manager_.handle_response(decoded);
        break;
    case MessageType::HEARTBEAT:
        heartbeat_manager_.handle_request(decoded.raw);
        break;
    case MessageType::HEARTBEAT_RESPONSE:
    {
        if (decoded.raw.len < sizeof(HeartbeatResponse)) {
            ESP_LOGW(
                TAG, "Malformed HEARTBEAT_RESPONSE: len %d < %d", (int)decoded.raw.len, (int)sizeof(HeartbeatResponse));
            return;
        }
        // MessageRouter just passes header.sender_node_id, resp->wifi_channel is ignored
        heartbeat_manager_.handle_response(decoded.header.sender_node_id);
        break;
    }
    case MessageType::ACK:
        tx_manager_.notify_logical_ack();
        break;
    case MessageType::CHANNEL_SCAN_PROBE:
        discovery_manager_.handle_probe(decoded.raw);
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
