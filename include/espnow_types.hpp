// include/espnow_types.hpp
#pragma once

#include <cstdint>
#include <cstring>
#include <tuple>

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "protocol_types.hpp"
#include "protocol_messages.hpp"

/**
 * @file espnow_types.hpp
 * @brief Internal and public data structures for the EspNowManager component.
 */

/** @brief Maximum number of peers that can be registered in the manager */
static constexpr uint8_t MAX_PEERS = 19;

/**
 * @brief Message delivered to the application layer after protocol processing.
 *
 * This struct decouples the application from the internal protocol details.
 * The rx_dispatch_task extracts the relevant fields from the decoded packet
 * and posts this to the app_rx_queue — the application never needs to know
 * about MessageHeader, RxPacket, or any ESP-NOW internals.
 *
 * Usage:
 *   AppMessage msg;
 *   xQueueReceive(app_queue, &msg, portMAX_DELAY);
 *   if (msg.payload_type == MyPayloadType::SENSOR_REPORT) {
 *       auto *report = reinterpret_cast<const SensorReport *>(msg.payload);
 *   }
 *   if (msg.requires_ack) {
 *       espnow.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
 *   }
 */
struct AppMessage
{
    NodeId sender_id;                  ///< Logical ID of the sending node
    NodeType sender_type;              ///< Role/type of the sending node
    MessageType msg_type;              ///< Type of the message, DATA, COMMAND...
    PayloadType payload_type;          ///< Application-defined payload identifier
    uint16_t sequence_number;          ///< Sequence number for ACK validation
    bool requires_ack;                 ///< If true, call confirm_reception() after processing
    uint8_t src_mac[6];                ///< MAC address of the sender
    int8_t rssi;                       ///< RSSI of the received signal (dBm)
    uint8_t payload[MAX_PAYLOAD_SIZE]; ///< Raw payload bytes (cast to your message struct)
    size_t payload_len;                ///< Number of valid bytes in payload[]
};

/**
 * @brief Generic structure for packets received from the ESP-NOW layer.
 */
struct RxPacket
{
    uint8_t src_mac[6];                 /**< Source MAC address of the sender */
    uint8_t data[ESP_NOW_MAX_DATA_LEN]; /**< Raw payload data */
    size_t len;                         /**< Length of the payload in bytes */
    int8_t rssi;                        /**< Received Signal Strength Indicator (dBm) */
    uint64_t timestamp_ms;              /**< Microsecond timestamp (converted to ms in rx_task) */
};

/**
 * @brief Structure for packets that have been decoded in rx_dispatch_task
 */
struct DecodedRxPacket
{
    RxPacket raw;         ///< Original complete packet
    MessageHeader header; ///< Decoded header
};

/**
 * @brief Detailed information about a registered peer.
 */
struct PeerInfo
{
    uint8_t mac[6];                 /**< 6-byte MAC address of the peer */
    NodeType type;                  /**< Categorization of the node (e.g., HUB or peripheral) */
    NodeId node_id;                 /**< Unique logical ID assigned to the node */
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
    bool paired;                    /**< Pairing status */
    uint32_t heartbeat_interval_ms; /**< Configured heartbeat interval */

    /**
     * @brief Custom equality operator to prevent padding byte evaluation.
     *
     * In C++, structs can contain hidden padding bytes for memory alignment.
     * Doing a direct memcmp() on the entire struct might cause false negatives
     * (i.e. identical data but different padding memory junk).
     * Using std::tie only compares the actual explicit data members safely.
     */
    bool operator==(const PersistentPeer& other) const
    {
        if (std::tie(type, node_id, paired, heartbeat_interval_ms) !=
            std::tie(other.type, other.node_id, other.paired, other.heartbeat_interval_ms)) {
            return false;
        }
        return std::memcmp(mac, other.mac, sizeof(mac)) == 0;
    }

    bool operator!=(const PersistentPeer& other) const { return !(*this == other); }
};

/**
 * @brief Structure for packets that are structured for transmission.
 * Allows TxManager to handle encoding, sequence numbers, and CRC.
 */
struct DecodedTxPacket
{
    uint8_t dest_mac[6];               ///< Destination MAC address
    MessageHeader header;              ///< Header to be encoded
    uint8_t payload[MAX_PAYLOAD_SIZE]; ///< Raw payload bytes
    size_t payload_len;                ///< Number of valid bytes in payload
};

/**
 * @brief Structure for packets queued for transmission (wire format).
 */
struct TxPacket
{
    uint8_t dest_mac[6];                /**< Destination MAC address (or BROADCAST) */
    uint8_t data[ESP_NOW_MAX_DATA_LEN]; /**< Raw data to be sent */
    size_t len;                         /**< Length of the data in bytes */
    bool requires_ack;                  /**< If true, logic will wait for a confirm_reception() */
};

/**
 * @brief Enumeration of node states.
 *
 * NodeState transitions: in src/node_state_machine.cpp
 */
enum class NodeState
{
    UNINITIALIZED = 0, ///< Initial state before initialization
    IDLE = 1,          ///< Initialized successfully, but not yet paired/idle
    PAIRING = 2,       ///< Actively advertising or accepting pairing requests
    OPERATIONAL = 3,   ///< Has peers, normal operation
    PAIRING_SCAN = 4,  ///< Scanning for a HUB to start pairing
    RECOVERY_SCAN = 5, ///< Lost connection to peers, rediscovering channel
    COUNT = 6          ///< Number of states (for validation)
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
 * @brief Structure for peer statistics.
 */
struct PeerStatistics
{
    NodeId node_id = 0;        ///< Node ID
    int8_t rssi_last = 0;      ///< Last received RSSI
    uint8_t rssi_alpha = 20;   ///< Alpha derived from heartbeat interval
    int8_t rssi_avg = 0;       ///< Exponential moving average
    uint32_t packets_rx = 0;   ///< Number of packets received
    uint32_t packets_tx = 0;   ///< Number of packets transmitted
    uint32_t packets_lost = 0; ///< Number of packets lost
    uint32_t retries = 0;      ///< Number of retries
    uint32_t rtt_last_ms = 0;  ///< Last round-trip time in milliseconds
    uint32_t rtt_avg_ms = 0;   ///< Average round-trip time in milliseconds
    uint8_t dirty_count = 0;   ///< Flush to storage when threshold reached
};

/**
 * @brief Structure for persistent peer statistics.
 */
struct PeerStatisticsPersist
{
    NodeId node_id = 0;        ///< Node ID
    int8_t rssi_avg = 0;       ///< Exponential moving average
    uint32_t packets_rx = 0;   ///< Number of packets received
    uint32_t packets_tx = 0;   ///< Number of packets transmitted
    uint32_t packets_lost = 0; ///< Number of packets lost
    uint32_t rtt_avg_ms = 0;   ///< Average round-trip time in milliseconds
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
    uint32_t channel_monitor_interval_ms; /**< Interval for channel monitoring (ms) */
    uint8_t scan_max_retries;             /**< Maximum retries for recovery scan. Defaults to SCAN_MAX_RETRIES. */

    uint32_t stack_size_rx_task;        /**< Stack size for the internal packet dispatcher task */
    uint32_t stack_size_tx_task;        /**< Stack size for the transmission manager task */
    uint32_t stack_size_discovery_task; /**< Stack size for the discovery task */

    UBaseType_t priority_rx_task;        /**< Priority for the internal packet dispatcher task */
    UBaseType_t priority_tx_task;        /**< Priority for the transmission manager task */
    UBaseType_t priority_discovery_task; /**< Priority for the discovery task */

    uint32_t rx_queue_length; /**< Length of the internal packet dispatcher queue */
    uint32_t tx_queue_length; /**< Length of the internal packet dispatcher queue */

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
        , channel_monitor_interval_ms(DEFAULT_CHANNEL_MONITOR_INTERVAL_MS)
        , scan_max_retries(SCAN_MAX_RETRIES)
        , stack_size_rx_task(4096)
        , stack_size_tx_task(4096)
        , priority_rx_task(10)
        , priority_tx_task(9)
        , rx_queue_length(30)
        , tx_queue_length(30)

    {
    }
};
