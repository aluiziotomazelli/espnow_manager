#pragma once

#include "protocol_types.hpp"

/**
 * @file protocol_messages.hpp
 * @brief Definition of protocol message structures for ESP-NOW communication.
 *
 * All structures in this file use 1-byte packing to ensure consistent memory layout
 * across different compilers and architectures.
 */

#pragma pack(push, 1)

/**
 * @brief Universal header included at the beginning of every packet.
 */
struct MessageHeader
{
    MessageType msg_type;     /**< Type identifier for the message */
    uint16_t sequence_number; /**< Incremental sequence number for tracking and deduplication */
    NodeType sender_type;     /**< Type/Role of the sending node */
    NodeId sender_node_id;    /**< Unique ID of the sending node */
    PayloadType payload_type; /**< Identifier for the content format (for DATA/COMMAND) */
    bool requires_ack;        /**< If true, the receiver should send an ACK message */
    NodeId dest_node_id;      /**< Unique ID of the destination node (or BROADCAST) */
    uint64_t timestamp_ms;    /**< Millisecond timestamp of when the message was sent */
};

// ========== TRANSPORT LAYER ==========

/**
 * @brief Packet sent by a Node to request pairing with a Hub.
 */
struct PairRequest
{
    MessageHeader header;            /**< Universal message header */
    uint8_t firmware_version[3];     /**< Current firmware version of the node (major, minor, patch) */
    uint64_t uptime_ms;              /**< Current uptime of the node in milliseconds */
    char device_name[16];            /**< Human-readable name of the device */
    uint32_t heartbeat_interval_ms; /**< Requested interval between heartbeats */
};

/**
 * @brief Packet sent by the Hub in response to a PairRequest.
 */
struct PairResponse
{
    MessageHeader header;            /**< Universal message header */
    PairStatus status;               /**< Acceptance or rejection status */
    NodeId assigned_id;              /**< Node ID assigned by the Hub (if accepted) */
    uint32_t heartbeat_interval_ms; /**< Heartbeat interval authorized by the Hub */
    uint32_t report_interval_ms;    /**< Suggested reporting interval for application data */
    uint8_t wifi_channel;            /**< WiFi channel the Hub is operating on */
};

/**
 * @brief Periodic packet sent by a Node to maintain its 'online' status.
 */
struct HeartbeatMessage
{
    MessageHeader header; /**< Universal message header */
    uint16_t battery_mv;  /**< Current battery voltage in millivolts */
    int8_t rssi;          /**< RSSI of the Hub as seen by the Node (from last reception) */
    uint64_t uptime_ms;   /**< Current uptime of the node in milliseconds */
};

/**
 * @brief Packet sent by the Hub as an immediate response to a HeartbeatMessage.
 */
struct HeartbeatResponse
{
    MessageHeader header;     /**< Universal message header */
    uint64_t server_time_ms;  /**< Current Unix epoch or relative server time in milliseconds */
    uint8_t wifi_channel;     /**< Current WiFi channel of the Hub (for channel synchronization) */
};

// ========== APPLICATION LAYER ==========

/**
 * @brief Packet sent to acknowledge receipt of a DATA or COMMAND message.
 */
struct AckMessage
{
    MessageHeader header;       /**< Universal message header */
    uint16_t ack_sequence;      /**< Sequence number of the message being acknowledged */
    AckStatus status;           /**< Processing status of the acknowledged message */
    uint32_t processing_time_us; /**< Time taken to process the message in microseconds */
};

/**
 * @brief Packet sent to initiate or manage an OTA update.
 */
struct OtaCommand
{
    MessageHeader header;      /**< Universal message header */
    CommandType cmd_type;      /**< Type of OTA command (e.g., START) */
    char firmware_url[128];    /**< URL where the node can download the new firmware */
    uint32_t firmware_size;    /**< Expected size of the firmware binary in bytes */
    uint8_t firmware_hash[32]; /**< SHA-256 hash of the firmware binary for validation */
};

#pragma pack(pop)

// Validações de tamanho para garantir que nenhum payload exceda o limite do ESP-NOW
static_assert(sizeof(MessageHeader) == MESSAGE_HEADER_SIZE, "MessageHeader size is incorrect");
static_assert(sizeof(PairRequest) <= MAX_PAYLOAD_SIZE, "PairRequest payload is too large");
static_assert(sizeof(PairResponse) <= MAX_PAYLOAD_SIZE, "PairResponse payload is too large");
static_assert(sizeof(HeartbeatMessage) <= MAX_PAYLOAD_SIZE, "HeartbeatMessage payload is too large");
static_assert(sizeof(HeartbeatResponse) <= MAX_PAYLOAD_SIZE, "HeartbeatResponse payload is too large");
static_assert(sizeof(AckMessage) <= MAX_PAYLOAD_SIZE, "AckMessage payload is too large");
static_assert(sizeof(OtaCommand) <= MAX_PAYLOAD_SIZE, "OtaCommand payload is too large");
