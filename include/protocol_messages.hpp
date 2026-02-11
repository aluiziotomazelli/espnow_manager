#pragma once
#include "protocol_types.hpp"

#pragma pack(push, 1)

// ========== HEADER UNIVERSAL ==========
struct MessageHeader
{
    MessageType msg_type;
    uint16_t sequence_number;
    NodeType sender_type;
    NodeId sender_node_id;
    PayloadType payload_type;
    bool requires_ack;
    NodeId dest_node_id;
    uint64_t timestamp_ms;
};

// ========== TRANSPORT LAYER ==========
struct PairRequest
{
    MessageHeader header;
    uint8_t firmware_version[3];
    uint64_t uptime_ms;
    char device_name[16];
    uint32_t heartbeat_interval_ms;
};

struct PairResponse
{
    MessageHeader header;
    PairStatus status;
    NodeId assigned_id;
    uint32_t heartbeat_interval_ms;
    uint32_t report_interval_ms;
    uint8_t wifi_channel;
};

struct HeartbeatMessage
{
    MessageHeader header;
    uint16_t battery_mv;
    int8_t rssi;
    uint64_t uptime_ms;
};

struct HeartbeatResponse
{
    MessageHeader header;
    uint64_t server_time_ms;
    uint8_t wifi_channel;
};

// ========== APPLICATION LAYER ==========
struct AckMessage
{
    MessageHeader header;
    uint16_t ack_sequence;
    AckStatus status;
    uint32_t processing_time_us;
};

struct WaterLevelReport
{
    MessageHeader header;
    uint16_t level_permille;
    float distance_cm;
    uint16_t battery_mv;
    UsQuality quality;
    UsFailure failure;
    bool float_switch_is_full;
    bool backup_mode_active;
};

struct SolarSensorReport
{
    MessageHeader header;
    uint16_t voltage_mv;
    uint16_t current_ma;
    uint16_t power_mw;
};

struct OtaCommand
{
    MessageHeader header;
    CommandType cmd_type;
    char firmware_url[128];
    uint32_t firmware_size;
    uint8_t firmware_hash[32];
};

#pragma pack(pop)

// Validações de tamanho para garantir que nenhum payload exceda o limite do ESP-NOW
static_assert(sizeof(MessageHeader) == MESSAGE_HEADER_SIZE,
              "MessageHeader size is incorrect");
static_assert(sizeof(PairRequest) <= MAX_PAYLOAD_SIZE, "PairRequest payload is too large");
static_assert(sizeof(PairResponse) <= MAX_PAYLOAD_SIZE,
              "PairResponse payload is too large");
static_assert(sizeof(HeartbeatMessage) <= MAX_PAYLOAD_SIZE,
              "HeartbeatMessage payload is too large");
static_assert(sizeof(HeartbeatResponse) <= MAX_PAYLOAD_SIZE,
              "HeartbeatResponse payload is too large");
static_assert(sizeof(AckMessage) <= MAX_PAYLOAD_SIZE, "AckMessage payload is too large");
static_assert(sizeof(WaterLevelReport) <= MAX_PAYLOAD_SIZE,
              "WaterLevelReport payload is too large");
static_assert(sizeof(SolarSensorReport) <= MAX_PAYLOAD_SIZE,
              "SolarSensorReport payload is too large");
static_assert(sizeof(OtaCommand) <= MAX_PAYLOAD_SIZE, "OtaCommand payload is too large");
