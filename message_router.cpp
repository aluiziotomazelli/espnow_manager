#include "message_router.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
#include <cstring>

static const char *TAG = "MessageRouter";

RealMessageRouter::RealMessageRouter(IPeerManager &peer_manager,
                                     ITxManager &tx_manager,
                                     IHeartbeatManager &heartbeat_manager,
                                     IPairingManager &pairing_manager,
                                     IMessageCodec &message_codec)
    : peer_manager_(peer_manager)
    , tx_manager_(tx_manager)
    , heartbeat_manager_(heartbeat_manager)
    , pairing_manager_(pairing_manager)
    , message_codec_(message_codec)
{
}

void RealMessageRouter::handle_packet(const RxPacket &packet)
{
    auto header_opt = message_codec_.decode_header(packet.data, packet.len);
    if (!header_opt) return;
    const MessageHeader &header = header_opt.value();

    tx_manager_.notify_link_alive();

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
    case MessageType::HEARTBEAT: {
        if (packet.len < sizeof(HeartbeatMessage)) {
            ESP_LOGW(TAG, "Malformed HEARTBEAT: len %d < %d", (int)packet.len, (int)sizeof(HeartbeatMessage));
            return;
        }
        auto msg = reinterpret_cast<const HeartbeatMessage *>(packet.data);
        heartbeat_manager_.handle_request(header.sender_node_id, packet.src_mac, msg->uptime_ms);
        break;
    }
    case MessageType::HEARTBEAT_RESPONSE: {
        if (packet.len < sizeof(HeartbeatResponse)) {
            ESP_LOGW(TAG, "Malformed HEARTBEAT_RESPONSE: len %d < %d", (int)packet.len, (int)sizeof(HeartbeatResponse));
            return;
        }
        auto resp = reinterpret_cast<const HeartbeatResponse *>(packet.data);
        heartbeat_manager_.handle_response(header.sender_node_id, resp->wifi_channel);
        // Note: Channel update should be handled by the observer/facade if needed
        break;
    }
    case MessageType::ACK:
        tx_manager_.notify_logical_ack();
        break;
    case MessageType::CHANNEL_SCAN_PROBE:
        handle_scan_probe(packet);
        break;
    case MessageType::CHANNEL_SCAN_RESPONSE: {
        uint8_t ch;
        esp_wifi_get_channel(&ch, nullptr);
        peer_manager_.add(header.sender_node_id, packet.src_mac, ch, header.sender_type);
        tx_manager_.notify_hub_found();
        break;
    }
    case MessageType::DATA:
    case MessageType::COMMAND:
        if (app_queue_) {
            if (xQueueSend(app_queue_, &packet, 0) != pdTRUE) {
                ESP_LOGW(TAG, "App queue full, dropping packet type %d", (int)header.msg_type);
            }
        }
        break;
    default:
        break;
    }
}

bool RealMessageRouter::should_dispatch_to_worker(MessageType type)
{
    switch (type) {
    case MessageType::PAIR_REQUEST:
    case MessageType::PAIR_RESPONSE:
    case MessageType::HEARTBEAT:
    case MessageType::HEARTBEAT_RESPONSE:
    case MessageType::ACK:
    case MessageType::CHANNEL_SCAN_PROBE:
    case MessageType::CHANNEL_SCAN_RESPONSE:
        return true;
    default:
        return false;
    }
}

void RealMessageRouter::handle_scan_probe(const RxPacket &packet)
{
    if (my_type_ != ReservedTypes::HUB) return;
    auto header_opt = message_codec_.decode_header(packet.data, packet.len);
    if (!header_opt) return;

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    MessageHeader resp;
    resp.msg_type = MessageType::CHANNEL_SCAN_RESPONSE;
    resp.sender_node_id = my_id_;
    resp.sender_type = my_type_;
    resp.dest_node_id = header_opt->sender_node_id;
    resp.sequence_number = 0;

    auto encoded = message_codec_.encode(resp, nullptr, 0);
    if (encoded.empty()) return;

    tx_packet.len = encoded.size();
    memcpy(tx_packet.data, encoded.data(), tx_packet.len);
    tx_packet.requires_ack = false;
    tx_manager_.queue_packet(tx_packet);
}
