#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "esp_now.h"

// Correct size of the universal message header
constexpr size_t MESSAGE_HEADER_SIZE = 16;
constexpr size_t CRC_SIZE            = 1;
// The maximum payload size is the total ESP-NOW size minus the header and CRC
constexpr size_t MAX_PAYLOAD_SIZE = ESP_NOW_MAX_DATA_LEN - MESSAGE_HEADER_SIZE - CRC_SIZE;

// Default values (can be overridden in config)
constexpr uint32_t DEFAULT_ACK_TIMEOUT_MS        = 500;
constexpr uint32_t DEFAULT_HEARTBEAT_INTERVAL_MS = 60000;
constexpr uint8_t DEFAULT_WIFI_CHANNEL           = 1;
constexpr float HEARTBEAT_OFFLINE_MULTIPLIER     = 2.5f;

// Constants for retry logic
constexpr uint32_t LOGICAL_ACK_TIMEOUT_MS = 500;
constexpr uint8_t MAX_LOGICAL_RETRIES     = 3;
constexpr uint8_t MAX_PHYSICAL_FAILURES   = 3;

constexpr uint16_t SCAN_CHANNEL_TIMEOUT_MS = 50;
constexpr uint8_t SCAN_CHANNEL_ATTEMPTS    = 2;
constexpr uint16_t MAX_SCAN_TIME_MS        = SCAN_CHANNEL_TIMEOUT_MS * SCAN_CHANNEL_ATTEMPTS * 20;

// Generic types for Node identification and categorization
using NodeId      = uint8_t;
using NodeType    = uint8_t;
using PayloadType = uint8_t;

namespace ReservedIds {
constexpr NodeId BROADCAST = 0xFF;
constexpr NodeId HUB       = 0x01;
} // namespace ReservedIds

namespace ReservedTypes {
constexpr NodeType UNKNOWN = 0x00;
constexpr NodeType HUB     = 0x01;
} // namespace ReservedTypes

// Bridge templates for type-safe enum usage
template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
constexpr NodeId to_node_id(T enum_val)
{
    return static_cast<NodeId>(enum_val);
}

template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeType)>>
constexpr NodeType to_node_type(T enum_val)
{
    return static_cast<NodeType>(enum_val);
}

template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(PayloadType)>>
constexpr PayloadType to_payload_type(T enum_val)
{
    return static_cast<PayloadType>(enum_val);
}

enum class MessageType : uint8_t
{
    PAIR_REQUEST          = 0x00,
    PAIR_RESPONSE         = 0x01,
    HEARTBEAT             = 0x02,
    HEARTBEAT_RESPONSE    = 0x03,
    DATA                  = 0x10,
    ACK                   = 0x11,
    COMMAND               = 0x20,
    CHANNEL_SCAN_PROBE    = 0x30,
    CHANNEL_SCAN_RESPONSE = 0x31,
};

enum class PairStatus : uint8_t
{
    ACCEPTED             = 0x00,
    REJECTED_NOT_ALLOWED = 0x01,
};

enum class AckStatus : uint8_t
{
    OK                 = 0x00,
    ERROR_INVALID_DATA = 0x01,
    ERROR_PROCESSING   = 0x02,
};

enum class CommandType : uint8_t
{
    START_OTA           = 0x01,
    REBOOT              = 0x02,
    SET_REPORT_INTERVAL = 0x03,
};
