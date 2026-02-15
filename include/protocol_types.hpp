#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "esp_now.h"

/**
 * @file protocol_types.hpp
 * @brief Common protocol types and constants for ESP-NOW communication.
 */

/** @brief Correct size of the universal message header */
constexpr size_t MESSAGE_HEADER_SIZE = 16;
/** @brief Size of the CRC field in the packet */
constexpr size_t CRC_SIZE            = 1;
/** @brief The maximum payload size is the total ESP-NOW size minus the header and CRC */
constexpr size_t MAX_PAYLOAD_SIZE = ESP_NOW_MAX_DATA_LEN - MESSAGE_HEADER_SIZE - CRC_SIZE;

// Default values (can be overridden in config)
/** @brief Default acknowledgment timeout in milliseconds */
constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS        = 500;
/** @brief Default interval between heartbeat messages in milliseconds */
constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS = 60000;
/** @brief Default WiFi channel to use if none is specified */
constexpr uint8_t DEFAULT_WIFI_CHANNEL           = 1;
/** @brief Multiplier applied to heartbeat interval to determine if a node is offline */
constexpr float HEARTBEAT_OFFLINE_MULTIPLIER     = 2.5f;

// Constants for retry logic
/** @brief Timeout for logical acknowledgments in milliseconds */
constexpr uint32_t LOGICAL_ACK_TIMEOUT_MS = 500;
/** @brief Maximum number of logical retries for unacknowledged packets */
constexpr uint8_t MAX_LOGICAL_RETRIES     = 3;
/** @brief Maximum number of physical transmission failures before giving up or scanning */
constexpr uint8_t MAX_PHYSICAL_FAILURES   = 3;

/** @brief Timeout for scanning a single channel during discovery (ms) */
constexpr uint16_t SCAN_CHANNEL_TIMEOUT_MS = 50;
/** @brief Number of scan attempts per channel */
constexpr uint8_t SCAN_CHANNEL_ATTEMPTS    = 2;
/** @brief Total maximum time allowed for a full channel scan */
constexpr uint16_t MAX_SCAN_TIME_MS        = SCAN_CHANNEL_TIMEOUT_MS * SCAN_CHANNEL_ATTEMPTS * 20;

/** @brief Type alias for Node identification (0-255) */
using NodeId      = uint8_t;
/** @brief Type alias for Node role/category categorization */
using NodeType    = uint8_t;
/** @brief Type alias for application-defined payload identifiers */
using PayloadType = uint8_t;

/**
 * @brief Reserved Node IDs with special meanings.
 */
namespace ReservedIds {
/** @brief Broadcast ID for sending to all nodes */
constexpr NodeId BROADCAST = 0xFF;
/** @brief Central Hub/Controller default ID */
constexpr NodeId HUB       = 0x01;
} // namespace ReservedIds

/**
 * @brief Reserved Node Types for core functionality.
 */
namespace ReservedTypes {
/** @brief Type for nodes that have not yet identified themselves */
constexpr NodeType UNKNOWN = 0x00;
/** @brief Type identifier for the Central Hub */
constexpr NodeType HUB     = 0x01;
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
    PAIR_REQUEST          = 0x00, /**< Initial request from a Node to pair with a Hub */
    PAIR_RESPONSE         = 0x01, /**< Response from the Hub to a pairing request */
    HEARTBEAT             = 0x02, /**< Periodic keep-alive message from Node to Hub */
    HEARTBEAT_RESPONSE    = 0x03, /**< Acknowledgment of heartbeat from Hub to Node */
    DATA                  = 0x10, /**< Standard application data packet */
    ACK                   = 0x11, /**< Logical acknowledgment for DATA or COMMAND packets */
    COMMAND               = 0x20, /**< Control command sent from Hub to Node */
    CHANNEL_SCAN_PROBE    = 0x30, /**< Broadcast probe sent during channel discovery */
    CHANNEL_SCAN_RESPONSE = 0x31, /**< Response to a scan probe to identify active Hubs */
};

/**
 * @brief Status codes for the pairing process.
 */
enum class PairStatus : uint8_t
{
    ACCEPTED             = 0x00, /**< Pairing successful; Node is registered */
    REJECTED_NOT_ALLOWED = 0x01, /**< Pairing failed; registration not permitted */
};

/**
 * @brief Logical acknowledgment status codes.
 */
enum class AckStatus : uint8_t
{
    OK                 = 0x00, /**< Message received and processed successfully */
    ERROR_INVALID_DATA = 0x01, /**< Message received but payload data is invalid */
    ERROR_PROCESSING   = 0x02, /**< Message received but processing failed internally */
};

/**
 * @brief Enumeration of standard control commands.
 */
enum class CommandType : uint8_t
{
    START_OTA           = 0x01, /**< Instructs the node to start an Over-The-Air update */
    REBOOT              = 0x02, /**< Instructs the node to perform a system reset */
    SET_REPORT_INTERVAL = 0x03, /**< Instructs the node to change its data reporting frequency */
};
