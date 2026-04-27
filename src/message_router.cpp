#include <cstring>

#include "esp_log.h"

#include "message_router.hpp"

namespace espnow {

static const char* TAG = "MessageRouter";

MessageRouter::MessageRouter(
    IDiscoveryManager& discovery_manager,
    ITxManager& tx_manager,
    IHeartbeatManager& heartbeat_manager,
    IPairingManager& pairing_manager)
    : discovery_manager_(discovery_manager)
    , tx_manager_(tx_manager)
    , heartbeat_manager_(heartbeat_manager)
    , pairing_manager_(pairing_manager)

{
}

void MessageRouter::handle_packet(const DecodedRxPacket& decoded)
{
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
        if (decoded.raw.len < sizeof(HeartbeatMessage)) {
            ESP_LOGW(TAG, "Malformed HEARTBEAT: len %d < %d", (int)decoded.raw.len, (int)sizeof(HeartbeatMessage));
            return;
        }
        heartbeat_manager_.handle_request(decoded);
        break;

    case MessageType::HEARTBEAT_RESPONSE:
    {
        if (decoded.raw.len < sizeof(HeartbeatResponse)) {
            ESP_LOGW(
                TAG, "Malformed HEARTBEAT_RESPONSE: len %d < %d", (int)decoded.raw.len, (int)sizeof(HeartbeatResponse));
            return;
        }
        heartbeat_manager_.handle_response(decoded);
        break;
    }
    case MessageType::ACK:
        if (decoded.raw.len < sizeof(AckMessage)) {
            ESP_LOGW(TAG, "Malformed ACK: len %d < %d", (int)decoded.raw.len, (int)sizeof(AckMessage));
            return;
        }
        tx_manager_.handle_ack(decoded);
        break;
    case MessageType::CHANNEL_SCAN_PROBE:
        if (decoded.raw.len < sizeof(MessageHeader)) {
            ESP_LOGW(
                TAG, "Malformed CHANNEL_SCAN_PROBE: len %d < %d", (int)decoded.raw.len, (int)sizeof(MessageHeader));
            return;
        }
        discovery_manager_.handle_scan_probe(decoded);
        break;
    case MessageType::CHANNEL_SCAN_RESPONSE:
        if (decoded.raw.len < sizeof(MessageHeader)) {
            ESP_LOGW(
                TAG, "Malformed CHANNEL_SCAN_RESPONSE: len %d < %d", (int)decoded.raw.len, (int)sizeof(MessageHeader));
            return;
        }
        discovery_manager_.handle_scan_response(decoded);
        break;

    default:
        break;
    }
}

} // namespace espnow
