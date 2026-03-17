#include <cstring>

#include "esp_log.h"
// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
// #include "freertos/task.h"
// #include "freertos/timers.h"

#include "i_message_codec.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"

#include "pairing_manager.hpp"
#include "protocol_types.hpp"

static const char *TAG = "PairingMgr";

PairingManager::PairingManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, IMessageCodec &codec)
    : tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , codec_(codec)

{
}

esp_err_t PairingManager::init(NodeId id, NodeType type)
{
    my_id_ = id;
    my_type_ = type;
    is_initialized_ = true;
    return ESP_OK;
}

void PairingManager::tick(uint64_t now_ms)
{
    if (!is_initialized_ || !is_active_)
        return;

    if (now_ms - started_at_ms_ >= timeout_ms_) {
        is_active_ = false;
        ESP_LOGI(TAG, "Pairing timed out.");
        return;
    }
    if (my_type_ != ReservedTypes::HUB && now_ms - last_request_ms_ >= periodic_interval_ms_) {
        send_pair_request();
        last_request_ms_ = now_ms;
    }
}

void PairingManager::set_channel(uint8_t channel)
{
    current_channel_ = channel;
}

esp_err_t PairingManager::start(uint32_t timeout_ms, uint64_t now_ms)
{
    if (!is_initialized_ || is_active_)
        return ESP_ERR_INVALID_STATE;

    timeout_ms_ = timeout_ms;
    started_at_ms_ = now_ms;
    last_request_ms_ = now_ms;
    is_active_ = true;

    if (my_type_ != ReservedTypes::HUB) {
        send_pair_request(); // Send initial pair request immediately
    }

    return ESP_OK;
}

void PairingManager::handle_request(const RxPacket &packet)
{
    // Only initialized and  active pairing session processes requests
    if (!is_initialized_ || !is_active_) {
        return;
    }
    // Only non-HUB nodes can send pair requests to the HUB
    if (my_type_ != ReservedTypes::HUB) {
        return;
    }

    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt) {
        return;
    }

    const MessageHeader &header = header_opt.value();
    const PairRequest *req = reinterpret_cast<const PairRequest *>(packet.data);

    ESP_LOGI(TAG, "Pair request from Node ID %d", (int)header.sender_node_id);

    PairResponse resp;
    resp.header.msg_type = MessageType::PAIR_RESPONSE;
    resp.header.sender_node_id = my_id_;
    resp.header.sender_type = my_type_;
    resp.header.dest_node_id = header.sender_node_id;
    resp.header.sequence_number = 0;

    if (header.sender_type == ReservedTypes::HUB) {
        resp.status = PairStatus::REJECTED_NOT_ALLOWED;
    }
    else {
        peer_mgr_.add(header.sender_node_id, packet.src_mac, header.sender_type, req->heartbeat_interval_ms);
        resp.status = PairStatus::ACCEPTED;
        resp.wifi_channel = current_channel_;
    }

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, packet.src_mac, 6);
    tx_packet.len = codec_.encode(
        resp.header,
        &resp.status,
        sizeof(PairResponse) - sizeof(MessageHeader),
        tx_packet.data,
        sizeof(tx_packet.data));
    if (tx_packet.len > 0) {
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

void PairingManager::handle_response(const RxPacket &packet)
{
    // Only initialized and active pairing session processes requests
    if (!is_initialized_ || !is_active_) {
        return;
    }
    // Only non-HUB nodes expect pair responses from the HUB
    if (my_type_ == ReservedTypes::HUB) {
        return;
    }

    auto header_opt = codec_.decode_header(packet.data, packet.len);
    if (!header_opt) {
        return;
    }

    const PairResponse *resp = reinterpret_cast<const PairResponse *>(packet.data);
    if (resp->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub on channel %d.", (int)resp->wifi_channel);
        peer_mgr_.add(header_opt->sender_node_id, packet.src_mac, header_opt->sender_type);
        is_active_ = false;
    }
}

void PairingManager::send_pair_request()
{
    PairRequest req;
    req.header.msg_type = MessageType::PAIR_REQUEST;
    req.header.sender_node_id = my_id_;
    req.header.sender_type = my_type_;
    req.header.dest_node_id = ReservedIds::HUB;
    req.header.sequence_number = 0;
    req.heartbeat_interval_ms = 60000;

    TxPacket tx_packet;
    memcpy(tx_packet.dest_mac, BROADCAST_MAC, 6);

    tx_packet.len = codec_.encode(
        req.header,
        &req.firmware_version,
        sizeof(PairRequest) - sizeof(MessageHeader),
        tx_packet.data,
        sizeof(tx_packet.data));
    if (tx_packet.len > 0) {
        tx_packet.requires_ack = false;
        tx_mgr_.queue_packet(tx_packet);
    }
}

bool PairingManager::is_active() const
{
    return is_active_;
}