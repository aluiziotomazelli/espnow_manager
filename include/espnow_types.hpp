#pragma once

#include "esp_now.h"
#include "protocol_types.hpp"
#include <cstdint>
#include <vector>

constexpr int MAX_PEERS = 19;

// Generic structure for received packets
struct RxPacket
{
    uint8_t src_mac[6];
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
    size_t len;
    int8_t rssi;
    int64_t timestamp_us;
};

// Public information about a peer
struct PeerInfo
{
    uint8_t mac[6];
    NodeType type;
    NodeId node_id;
    uint8_t channel;
    uint64_t last_seen_ms;
    bool paired;
    uint32_t heartbeat_interval_ms;
};

/**
 * @brief Peer information to be persisted.
 */
struct PersistentPeer
{
    uint8_t mac[6];
    NodeType type;
    NodeId node_id;
    uint8_t channel;
    bool paired;
    uint32_t heartbeat_interval_ms;
};

// --- FSM and TX Task Structures ---
struct TxPacket
{
    uint8_t dest_mac[6];
    uint8_t data[ESP_NOW_MAX_DATA_LEN];
    size_t len;
    bool requires_ack;
};

enum class TxState
{
    IDLE,
    SENDING,
    WAITING_FOR_ACK,
    RETRYING,
    SCANNING
};

struct PendingAck
{
    uint16_t sequence_number;
    uint64_t timestamp_ms;
    uint8_t retries_left;
    TxPacket packet;
};
