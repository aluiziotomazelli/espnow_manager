// include/espnow_types.hpp
#pragma once

#include <cstdint>
#include <cstring>
#include <tuple>

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "protocol_types.hpp"
#include "protocol_messages.hpp"
namespace espnow {

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
    int64_t timestamp_us;               /**< Microsecond timestamp (esp_timer_get_time) */
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
    int64_t last_seen_ms;           /**< Timestamp of the last message received (ms) */
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
    EventGroupHandle_t ack_event_group =
        nullptr; ///< Event group for ACKs, internally managed by TxManager::queue_packet
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
    int64_t timestamp_us;     /**< Timestamp of the last attempt (us) */
    uint8_t retries_left;     /**< Remaining retransmission attempts */
    TxPacket packet;          /**< Copy of the packet to allow retransmission */
    NodeId node_id;           /**< Target Node ID for tracking and timeout logic */
};

/**
 * @brief Structure for peer statistics.
 */
struct PeerStatistics
{
    NodeId node_id = 0;             ///< Node ID
    int8_t rssi_last = 0;           ///< Last received RSSI
    uint8_t rssi_alpha = 20;        ///< Alpha derived from heartbeat interval (default = 20)
    int8_t rssi_avg = -127;         ///< Exponential moving average (-127 = unknown)
    uint32_t packets_rx = 0;        ///< Number of packets received
    uint32_t packets_sent = 0;      ///< Successfully transmitted over the air
    uint32_t driver_errors = 0;     ///< hal_esp_now_send() returned error (NO_MEM, etc.)
    uint32_t delivery_failures = 0; ///< ESP-NOW callback reported ESP_NOW_SEND_FAIL
    uint32_t packets_lost = 0;      ///< Number of packets lost (ACK timeout after retries)
    uint32_t retries = 0;           ///< Number of retries
    uint32_t rtt_last_us = 0;       ///< Last round-trip time in microseconds
    uint32_t rtt_avg_us = 0;        ///< Average round-trip time in microseconds (0 = unknown)
};

/**
 * @brief Structure for persistent peer statistics.
 */
struct PeerStatisticsPersist
{
    NodeId node_id = 0;             ///< Node ID
    int8_t rssi_avg = -127;         ///< Exponential moving average
    uint32_t packets_rx = 0;        ///< Number of packets received
    uint32_t packets_sent = 0;      ///< Successfully transmitted over the air
    uint32_t driver_errors = 0;     ///< hal_esp_now_send() returned error
    uint32_t delivery_failures = 0; ///< ESP-NOW callback reported ESP_NOW_SEND_FAIL
    uint32_t packets_lost = 0;      ///< Number of packets lost (ACK timeout after retries)
    uint32_t retries = 0;           ///< Number of retries
    uint32_t rtt_avg_us = 0;        ///< Average round-trip time in microseconds
};

/**
 * @brief Controls whether the DiscoveryManager is allowed to change the WiFi
 *        channel when scanning for the hub.
 *
 * Use SCAN when the node is not connected to any AP (standalone ESP-NOW only).
 * Use FIXED when the node is connected to a WiFi AP: the channel is locked by
 * the AP association and cannot be changed; both the hub and this node are
 * assumed to be on the same AP channel.
 *
 * Default: SCAN (preserves original behavior).
 */
enum class ChannelPolicy : uint8_t
{
    SCAN,  ///< Node may call wifi_set_channel() during discovery scan (standalone mode)
    FIXED, ///< Node is WiFi-connected; channel is owned by the AP — no scanning allowed
};

/**
 * @brief Configuration structure for initializing the EspNowManager.
 */
struct EspNowConfig
{
    NodeId        node_id{ReservedIds::HUB};          /**< Logical ID for this device */
    NodeType      node_type{ReservedTypes::UNKNOWN};  /**< Role/Type for this device */
    QueueHandle_t app_rx_queue{nullptr};              /**< Handle to the application queue where incoming DATA/COMMANDS are posted */
    uint8_t       wifi_channel{DEFAULT_WIFI_CHANNEL}; /**< Initial WiFi channel to operate on */
    uint32_t      ack_timeout_ms{DEFAULT_ACK_TIMEOUT_MS}; /**< Timeout for logical acknowledgments (ms) */
    uint32_t      heartbeat_interval_ms{DEFAULT_HEARTBEAT_INTERVAL_MS}; /**< Interval for heartbeats; 0 disables generation (ms) */
    bool          enable_heartbeat{true};             /**< If false, autonomous heartbeat packet generation is disabled */
    uint32_t      channel_monitor_interval_ms{DEFAULT_CHANNEL_MONITOR_INTERVAL_MS}; /**< Interval for channel monitoring (ms) */
    uint8_t       scan_max_retries{SCAN_MAX_RETRIES}; /**< Maximum retries for recovery scan. Defaults to SCAN_MAX_RETRIES. */
    uint8_t       logical_ack_retries{0};             /**< Maximum retries for logical ACK timeout. Defaults to 0 (no resend on L7 timeout). */

    uint32_t      stack_size_rx_task{6144};           /**< Stack size for the internal packet dispatcher task */
    uint32_t      stack_size_tx_task{6144};           /**< Stack size for the transmission manager task */
    uint32_t      stack_size_discovery_task{3072};    /**< Stack size for the discovery task */

    UBaseType_t   priority_rx_task{10};               /**< Priority for the internal packet dispatcher task */
    UBaseType_t   priority_tx_task{9};                /**< Priority for the transmission manager task */
    UBaseType_t   priority_discovery_task{8};         /**< Priority for the discovery task */

    uint32_t      rx_queue_length{30};                /**< Length of the internal packet dispatcher queue */
    uint32_t      tx_queue_length{30};                /**< Length of the internal packet dispatcher queue */
};

} // namespace espnow
