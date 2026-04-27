// include/internface/i_heartbeat_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"

#include "espnow_types.hpp"
namespace espnow {

/**
 * @interface IHeartbeatManager
 * @brief Heartbeat generation and monitoring.
 * @internal
 */
class IHeartbeatManager
{
public:
    virtual ~IHeartbeatManager() = default;

    /**
     * @brief Initializes the heartbeat manager.
     * @param id Node ID.
     * @param type Node type.
     * @param interval_ms Heartbeat interval in milliseconds.
     */
    virtual void init(NodeId id, NodeType type, uint32_t interval_ms) = 0;

    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    void init(T1 id, T2 type, uint32_t interval_ms)
    {
        init(static_cast<NodeId>(id), static_cast<NodeType>(type), interval_ms);
    }

    /**
     * @brief Deinitializes the heartbeat manager.
     */
    virtual void deinit() = 0;

    /**
     * @brief Ticks the heartbeat manager.
     * @param now_ms Current time in milliseconds.
     */
    virtual void tick(int64_t now_ms) = 0;

    /**
     * @brief Sets the heartbeat interval in milliseconds.
     * @param heartbeat_interval_ms Heartbeat interval in milliseconds.
     */
    virtual void set_interval_ms(uint32_t heartbeat_interval_ms) = 0;

    /**
     * @brief Handles incoming heartbeat response packets.
     * @param decoded The decoded response packet containing RSSI.
     */
    virtual void handle_response(const DecodedRxPacket& decoded) = 0;

    /**
     * @brief Handles incoming heartbeat request packets.
     * @param decoded Decoded packet.
     */
    virtual void handle_request(const DecodedRxPacket& decoded) = 0;
};
} // namespace espnow
