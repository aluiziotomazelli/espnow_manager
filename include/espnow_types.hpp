#pragma once

#include <cstdint>
#include <vector>

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "protocol_types.hpp"

/**
 * @file espnow_types.hpp
 * @brief Internal and public data structures for the EspNowManager component.
 */

/** @brief Maximum number of peers that can be registered in the manager */
constexpr int MAX_PEERS = 19;

/**
 * @brief Generic structure for packets received from the ESP-NOW layer.
 */
struct RxPacket
{
    uint8_t src_mac[6];                 /**< Source MAC address of the sender */
    uint8_t data[ESP_NOW_MAX_DATA_LEN]; /**< Raw payload data */
    size_t len;                         /**< Length of the payload in bytes */
    int8_t rssi;                        /**< Received Signal Strength Indicator (dBm) */
    int64_t timestamp_us;               /**< Microsecond timestamp (esp_timer_get_time) */
};

/**
 * @brief Detailed information about a registered peer.
 */
struct PeerInfo
{
    uint8_t mac[6];                 /**< 6-byte MAC address of the peer */
    NodeType type;                  /**< Categorization of the node (e.g., HUB or peripheral) */
    NodeId node_id;                 /**< Unique logical ID assigned to the node */
    uint8_t channel;                /**< WiFi channel the peer is currently using */
    uint64_t last_seen_ms;          /**< Timestamp of the last message received (ms) */
    bool paired;                    /**< If true, the node has completed the pairing process */
    uint32_t heartbeat_interval_ms; /**< Expected frequency of heartbeat messages */
};

/**
 * @brief Peer information optimized for persistent storage (NVS/RTC).
 */
struct PersistentPeer
{
    uint8_t mac[6];                 /**< 6-byte MAC address */
    NodeType type;                  /**< Node type */
    NodeId node_id;                 /**< Logical Node ID */
    uint8_t channel;                /**< Operating WiFi channel */
    bool paired;                    /**< Pairing status */
    uint32_t heartbeat_interval_ms; /**< Configured heartbeat interval */
};

// --- FSM and TX Task Structures ---

/**
 * @brief Structure for packets queued for transmission.
 */
struct TxPacket
{
    uint8_t dest_mac[6];                /**< Destination MAC address (or BROADCAST) */
    uint8_t data[ESP_NOW_MAX_DATA_LEN]; /**< Raw data to be sent */
    size_t len;                         /**< Length of the data in bytes */
    bool requires_ack;                  /**< If true, logic will wait for a confirm_reception() */
};

/**
 * @brief Enumeration of internal transmission states.
 */
enum class TxState
{
    IDLE,            /**< No active transmission */
    SENDING,         /**< Waiting for the physical ESP-NOW callback */
    WAITING_FOR_ACK, /**< Physical send success, waiting for logical AckMessage */
    RETRYING,        /**< Waiting before attempting a retransmission */
    SCANNING         /**< Performing a channel scan to locate the destination */
};

/**
 * @brief Internal tracking structure for messages waiting for an acknowledgment.
 */
struct PendingAck
{
    uint16_t sequence_number; /**< Sequence number of the message being tracked */
    uint64_t timestamp_ms;    /**< Timestamp of the last attempt (ms) */
    uint8_t retries_left;     /**< Remaining retransmission attempts */
    TxPacket packet;          /**< Copy of the packet to allow retransmission */
    NodeId node_id;           /**< Target Node ID for tracking and timeout logic */
};

/**
 * @brief Configuration structure for initializing the EspNowManager.
 */
struct EspNowConfig
{
    NodeId node_id;                 /**< Logical ID for this device */
    NodeType node_type;             /**< Role/Type for this device */
    QueueHandle_t app_rx_queue;     /**< Handle to the application queue where incoming DATA/COMMANDS are posted */
    uint8_t wifi_channel;           /**< Initial WiFi channel to operate on */
    uint32_t ack_timeout_ms;        /**< Timeout for logical acknowledgments (ms) */
    uint32_t heartbeat_interval_ms; /**< Interval for heartbeats; 0 disables generation (ms) */

    uint32_t stack_size_rx_dispatch;      /**< Stack size for the internal packet dispatcher task */
    uint32_t stack_size_transport_worker; /**< Stack size for the worker task (Heartbeats, Pairing) */
    uint32_t stack_size_tx_manager;       /**< Stack size for the transmission manager task */

    /**
     * @brief Default constructor with sensible defaults.
     */
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
