#pragma once
#include "protocol_messages.hpp"
#include "protocol_types.hpp"

#pragma pack(push, 1)

/**
 * @brief Application-specific Node IDs for the Irrigation project.
 * These are mapped to the generic NodeId (uint8_t).
 */
enum class IrrigationNodeId : NodeId
{
    WATER_TANK   = 5,
    SOLAR_SENSOR = 7,
    PUMP_CONTROL = 10,
    WEATHER      = 12,
};

/**
 * @brief Application-specific Node Types for the Irrigation project.
 */
enum class IrrigationNodeType : NodeType
{
    SENSOR   = 2,
    ACTUATOR = 3,
};

/**
 * @brief Application-specific Payload Types for the Irrigation project.
 */
enum class IrrigationPayloadType : PayloadType
{
    WATER_LEVEL_REPORT     = 0x01,
    SOLAR_SENSOR_REPORT    = 0x02,
    WEATHER_REPORT         = 0x03,
    LOAD_CONTROLLER_STATUS = 0x04,
};

enum class UsQuality : uint8_t
{
    OK,      /**< Measurement is reliable and within expected parameters. */
    WEAK,    /**< Measurement is valid but may have reduced accuracy. */
    INVALID, /**< Measurement is unreliable and should be discarded. */
};

enum class UsFailure : uint8_t
{
    NONE,          /**< No failure occurred. */
    TIMEOUT,       /**< The echo pulse was not received within the timeout period. */
    HW_ERROR,      /**< A hardware-level error, such as a stuck ECHO pin. */
    INVALID_PULSE, /**< The measured pulse corresponds to a distance outside the valid
                      range. */
    HIGH_VARIANCE, /**< The variance among valid pings is too high, indicating
                      instability. */
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

#pragma pack(pop)

// Validações de tamanho para garantir que nenhum payload exceda o limite do ESP-NOW
static_assert(sizeof(WaterLevelReport) <= MAX_PAYLOAD_SIZE,
              "WaterLevelReport payload is too large");
static_assert(sizeof(SolarSensorReport) <= MAX_PAYLOAD_SIZE,
              "SolarSensorReport payload is too large");
