#include <cstring>

#include "esp_log.h"

#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"

#include "pairing_manager.hpp"
#include "protocol_types.hpp"

static const char* TAG = "PairingMgr";

PairingManager::PairingManager(
    ITxManager& tx_mgr,
    IPeerManager& peer_mgr,
    IFreeRTOSHAL& hal_freertos,
    ITimerHAL& hal_timer)
    : tx_mgr_(tx_mgr)
    , peer_mgr_(peer_mgr)
    , hal_freertos_(hal_freertos)
    , hal_timer_(hal_timer)
{
}

esp_err_t PairingManager::init(NodeId id, NodeType type, TaskHandle_t rx_task_handle, uint32_t heartbeat_interval_ms)
{
    if (rx_task_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (is_initialized_) {
        return ESP_ERR_INVALID_STATE;
    }

    my_id_ = id;
    my_type_ = type;
    rx_task_handle_ = rx_task_handle;
    heartbeat_interval_ms_ = heartbeat_interval_ms;
    is_initialized_ = true;

    return ESP_OK;
}

void PairingManager::deinit()
{
    is_initialized_ = false;
    is_active_ = false;
    rx_task_handle_ = nullptr;
}

void PairingManager::tick(uint64_t now_ms)
{
    if (!is_initialized_ || !is_active_) {
        return;
    }

    if (now_ms - started_at_ms_ >= timeout_ms_) {
        is_active_ = false;
        ESP_LOGI(TAG, "Pairing timed out.");
        notify_rx_task_pairing_done();
        return;
    }
    if (my_type_ != ReservedTypes::HUB && now_ms - last_request_ms_ >= periodic_interval_ms_) {
        send_pair_request();
        last_request_ms_ = now_ms;
    }
}

esp_err_t PairingManager::start(uint32_t timeout_ms, uint64_t now_ms)
{
    if (!is_initialized_ || is_active_) {
        return ESP_ERR_INVALID_STATE;
    }

    timeout_ms_ = timeout_ms;
    started_at_ms_ = now_ms;
    last_request_ms_ = now_ms;
    is_active_ = true;

    if (my_type_ != ReservedTypes::HUB) {
        send_pair_request(); // Send initial pair request immediately
    }

    return ESP_OK;
}

void PairingManager::handle_request(const DecodedRxPacket& decoded)
{
    // Only initialized and  active pairing session processes requests
    if (!is_initialized_ || !is_active_) {
        return;
    }
    // Only HUB handle pair requests; Nodes only expect pair responses from the HUB
    if (my_type_ != ReservedTypes::HUB) {
        return;
    }

    const MessageHeader& header = decoded.header;
    // PairRequest is packed — safe to reinterpret raw data directly
    const PairRequest* req = reinterpret_cast<const PairRequest*>(decoded.raw.data);

    ESP_LOGI(TAG, "Pair request from Node ID %d", (int)header.sender_node_id);

    DecodedTxPacket tx_packet{};
    memcpy(tx_packet.dest_mac, decoded.raw.src_mac, 6);

    uint64_t now_ms = get_time_ms();

    tx_packet.header.msg_type = MessageType::PAIR_RESPONSE;
    tx_packet.header.sender_node_id = my_id_;
    tx_packet.header.sender_type = my_type_;
    tx_packet.header.dest_node_id = header.sender_node_id;
    tx_packet.header.timestamp_ms = now_ms;

    PairStatus status;
    if (header.sender_type == ReservedTypes::HUB) {
        status = PairStatus::REJECTED_NOT_ALLOWED;
    }
    else {
        peer_mgr_.add(header.sender_node_id, decoded.raw.src_mac, header.sender_type, req->heartbeat_interval_ms);
        status = PairStatus::ACCEPTED;
    }

    PairResponse resp{};
    resp.status = status;
    // assigned_id, heartbeat_interval_ms, report_interval_ms remain zero for now

    // Copy only the payload portion of PairResponse, skipping MessageHeader.
    // &resp.status points to the first field after the header.
    tx_packet.payload_len = sizeof(PairResponse) - sizeof(MessageHeader);
    memcpy(tx_packet.payload, &resp.status, tx_packet.payload_len);

    tx_mgr_.queue_packet(tx_packet);
}

void PairingManager::handle_response(const DecodedRxPacket& decoded)
{
    // Only initialized and active pairing session processes requests
    if (!is_initialized_ || !is_active_) {
        return;
    }
    // Only non-HUB nodes expect pair responses from the HUB
    if (my_type_ == ReservedTypes::HUB) {
        return;
    }

    const PairResponse* resp = reinterpret_cast<const PairResponse*>(decoded.raw.data);
    if (resp->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub");
        peer_mgr_.add(decoded.header.sender_node_id, decoded.raw.src_mac, decoded.header.sender_type);
        is_active_ = false;
        notify_rx_task_pairing_done();
    }
}

void PairingManager::send_pair_request()
{
    DecodedTxPacket tx_packet{};
    memcpy(tx_packet.dest_mac, BROADCAST_MAC, 6);

    tx_packet.header.msg_type = MessageType::PAIR_REQUEST;
    tx_packet.header.sender_node_id = my_id_;
    tx_packet.header.sender_type = my_type_;
    tx_packet.header.dest_node_id = ReservedIds::HUB;
    tx_packet.header.timestamp_ms = get_time_ms();

    PairRequest req{};
    req.heartbeat_interval_ms = heartbeat_interval_ms_;
    // other fiels remain zero for now

    // Copy only the payload portion of PairRequest, skipping MessageHeader.
    // &req.heartbeat_interval_ms points to the first field after the header.
    tx_packet.payload_len = sizeof(PairRequest) - sizeof(MessageHeader);
    memcpy(tx_packet.payload, &req.heartbeat_interval_ms, tx_packet.payload_len);

    tx_mgr_.queue_packet(tx_packet);
}

void PairingManager::notify_rx_task_pairing_done()
{
    if (rx_task_handle_ != nullptr) {
        hal_freertos_.task_notify(rx_task_handle_, NOTIFY_PAIRING_DONE, eSetBits);
    }
}

uint64_t PairingManager::get_time_ms() const
{
    return hal_timer_.get_time_us() / 1000;
}