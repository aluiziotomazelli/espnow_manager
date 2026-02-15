#pragma once

#include "esp_now.h"
#include "protocol_types.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
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
    NodeId node_id;
};

// Configuration to initialize the EspNow component
struct EspNowConfig
{
    NodeId node_id;
    NodeType node_type;
    QueueHandle_t app_rx_queue;
    uint8_t wifi_channel;
    uint32_t ack_timeout_ms;
    uint32_t heartbeat_interval_ms;

    uint32_t stack_size_rx_dispatch;
    uint32_t stack_size_transport_worker;
    uint32_t stack_size_tx_manager;

    // Default constructor
    EspNowConfig()
        : node_id(ReservedIds::HUB)
        , node_type(ReservedTypes::UNKNOWN)
        , app_rx_queue(nullptr)
        , wifi_channel(DEFAULT_WIFI_CHANNEL)
        , ack_timeout_ms(DEFAULT_ACK_TIMEOUT_MS)
        , heartbeat_interval_ms(DEFAULT_HEARTBEAT_INTERVAL_MS)
        , stack_size_rx_dispatch(4096)
        , stack_size_transport_worker(5120)
        , stack_size_tx_manager(4096)
    {
    }
};
