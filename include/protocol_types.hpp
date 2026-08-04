#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "esp_now.h"
namespace espnow {

/**
 * @file protocol_types.hpp
 * @brief Common protocol types and constants for ESP-NOW communication.
 */

/**
 * @brief Notification Bits for tasks notification
 */
static constexpr uint32_t NOTIFY_LOGICAL_ACK = 0x01;      ///< Sent by TxManager when a valid ACK packet arrives
static constexpr uint32_t NOTIFY_DELIVERY_FAILURE = 0x02; ///< Sent by TxManager after esp_now_send_cb reports FAIL
static constexpr uint32_t NOTIFY_MAX_FAILURES = 0x04;     ///< Sent by TxManager when retries exhausted
static constexpr uint32_t NOTIFY_DELIVERY_SUCCESS = 0x08; ///< Sent by TxManager after esp_now_send_cb reports SUCCESS
static constexpr uint32_t NOTIFY_DATA = 0x10;             ///< Sent by TxManager to wake tx_task when a packet is queued
static constexpr uint32_t NOTIFY_ACK_TIMEOUT = 0x20;      ///< Set by TxManager's ack timer callback
static constexpr uint32_t NOTIFY_TASK_TO_STOP = 0x40;     ///< Sent to signal_task_to_stop()
static constexpr uint32_t NOTIFY_LINK_ALIVE = 0x80;       ///< Sent on any valid packet reception
static constexpr uint32_t NOTIFY_START_SCAN = 0x100;      ///< Sent by DiscoveryManager::start_scan()
static constexpr uint32_t NOTIFY_STOP_SCAN = 0x200;       ///< Sent by DiscoveryManager::stop_scan()
static constexpr uint32_t NOTIFY_SCAN_RESPONSE = 0x400;   ///< Sent when a scan response packet arrives
static constexpr uint32_t NOTIFY_CHANNEL_FOUND = 0x800;   ///< Sent by discovery_task() when hub is found
static constexpr uint32_t NOTIFY_SCAN_FAILED = 0x1000;    ///< Sent by discovery_task() when all channels exhausted
static constexpr uint32_t NOTIFY_CHANNEL_CHANGED = 0x2000; ///< Sent when WiFi channel drift is detected
static constexpr uint32_t NOTIFY_PAIRING_DONE = 0x4000;    ///< Sent by PairingManager when pairing timeout or success
static constexpr uint32_t NOTIFY_PEER_ADDED = 0x10000; ///< Sent by PairingManager after peer_mgr_.add() during pairing
static constexpr uint32_t NOTIFY_ALL = 0xFFFFFFFF;

static constexpr uint8_t BROADCAST_MAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/** @brief Correct size of the universal message header */
constexpr size_t MESSAGE_HEADER_SIZE = 17;
/** @brief Size of the CRC field in the packet */
constexpr size_t CRC_SIZE = 1;
/** @brief The maximum payload size is the total ESP-NOW size minus the header and CRC */
constexpr size_t MAX_PAYLOAD_SIZE = ESP_NOW_MAX_DATA_LEN - MESSAGE_HEADER_SIZE - CRC_SIZE;

// Default values (can be overridden in config)
/** @brief Default acknowledgment timeout in milliseconds */
constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS = 300;
/** @brief Default interval between heartbeat messages in milliseconds */
constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS = 60000;
/** @brief Default WiFi channel to use if none is specified */
constexpr uint8_t DEFAULT_WIFI_CHANNEL = 1;
/** @brief Default interval for channel monitoring (ms) */
constexpr uint32_t DEFAULT_CHANNEL_MONITOR_INTERVAL_MS = 1000;
/** @brief Multiplier applied to heartbeat interval to determine if a node is offline */
constexpr uint8_t HEARTBEAT_OFFLINE_MULTIPLIER = 3;
static constexpr uint32_t PAIRING_TIMEOUT_MS = 60000;
static constexpr uint32_t PAIRING_PERIODIC_INTERVAL_MS = 5000;

/** @brief Maximum number of physical transmission failures before giving up or scanning */
constexpr uint8_t MAX_FAILURES = 3;

/** @brief Maximum number of recovery scan retries (exponential backoff: 2+4+8+...+128s ≈ 4m14s total) */
constexpr uint8_t SCAN_MAX_RETRIES = 7;
/** @brief Base backoff duration for the first recovery scan retry (ms); doubles each attempt */
constexpr uint32_t SCAN_BACKOFF_BASE_MS = 2000;

/** @brief Timeout for scanning a single channel during discovery (ms) */
constexpr uint16_t SCAN_CHANNEL_TIMEOUT_MS = 50;
/** @brief Number of scan attempts per channel */
constexpr uint8_t SCAN_CHANNEL_ATTEMPTS = 1;
/** @brief Total maximum time allowed for a full channel scan */
constexpr uint16_t MAX_SCAN_TIME_MS = SCAN_CHANNEL_TIMEOUT_MS * SCAN_CHANNEL_ATTEMPTS * 13 + 500;

/** @brief Threshold for statistics flush */
constexpr uint8_t FLUSH_THRESHOLD_RX = 50;
constexpr uint8_t FLUSH_THRESHOLD_TX = 50;
constexpr uint8_t FLUSH_THRESHOLD_TX_FAILURE = 10;
constexpr uint8_t FLUSH_THRESHOLD_LOSS = 10;
constexpr uint8_t FLUSH_THRESHOLD_RTT = 30;

/** @brief Type alias for Node identification (0-255) */
using NodeId = uint8_t;
/** @brief Type alias for Node role/category categorization */
using NodeType = uint8_t;
/** @brief Type alias for application-defined payload identifiers */
using PayloadType = uint8_t;

/**
 * @brief Reserved Node IDs with special meanings.
 */
namespace ReservedIds {
/** @brief Broadcast ID for sending to all nodes */
constexpr NodeId BROADCAST = 0xFF;
/** @brief Central Hub/Controller default ID */
constexpr NodeId HUB = 0x01;
} // namespace ReservedIds

/**
 * @brief Reserved Node Types for core functionality.
 */
namespace ReservedTypes {
/** @brief Type for nodes that have not yet identified themselves */
constexpr NodeType UNKNOWN = 0x00;
/** @brief Type identifier for the Central Hub */
constexpr NodeType HUB = 0x01;
} // namespace ReservedTypes

/**
 * @brief Bridge template for type-safe NodeId conversion from enums
 */
template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
constexpr NodeId to_node_id(T enum_val)
{
    return static_cast<NodeId>(enum_val);
}

/**
 * @brief Bridge template for type-safe NodeType conversion from enums
 */
template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeType)>>
constexpr NodeType to_node_type(T enum_val)
{
    return static_cast<NodeType>(enum_val);
}

/**
 * @brief Bridge template for type-safe PayloadType conversion from enums
 */
template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(PayloadType)>>
constexpr PayloadType to_payload_type(T enum_val)
{
    return static_cast<PayloadType>(enum_val);
}

/**
 * @brief Enumeration of protocol-level message types.
 */
enum class MessageType : uint8_t
{
    PAIR_REQUEST = 0x00,          /**< Initial request from a Node to pair with a Hub */
    PAIR_RESPONSE = 0x01,         /**< Response from the Hub to a pairing request */
    HEARTBEAT = 0x02,             /**< Periodic keep-alive message from Node to Hub */
    HEARTBEAT_RESPONSE = 0x03,    /**< Acknowledgment of heartbeat from Hub to Node */
    DATA = 0x10,                  /**< Standard application data packet */
    ACK = 0x11,                   /**< Logical acknowledgment for DATA or COMMAND packets */
    COMMAND = 0x20,               /**< Control command sent from Hub to Node */
    CHANNEL_SCAN_PROBE = 0x30,    /**< Broadcast probe sent during channel discovery */
    CHANNEL_SCAN_RESPONSE = 0x31, /**< Response to a scan probe to identify active Hubs */
};

/**
 * @brief Status codes for the pairing process.
 */
enum class PairStatus : uint8_t
{
    ACCEPTED = 0x00,             /**< Pairing successful; Node is registered */
    REJECTED_NOT_ALLOWED = 0x01, /**< Pairing failed; registration not permitted */
};

/**
 * @brief Logical acknowledgment status codes.
 */
enum class AckStatus : uint8_t
{
    OK = 0x00,                 /**< Message received and processed successfully */
    ERROR_INVALID_DATA = 0x01, /**< Message received but payload data is invalid */
    ERROR_PROCESSING = 0x02,   /**< Message received but processing failed internally */
};

/**
 * @brief Enumeration of standard control commands.
 */
enum class CommandType : uint8_t
{
    START_OTA = 0x01,           /**< Instructs the node to start an Over-The-Air update */
    REBOOT = 0x02,              /**< Instructs the node to perform a system reset */
    SET_REPORT_INTERVAL = 0x03, /**< Instructs the node to change its data reporting frequency */
};

} // namespace espnow
