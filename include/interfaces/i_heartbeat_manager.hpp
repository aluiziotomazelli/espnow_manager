// include/internface/i_heartbeat_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"

#include "espnow_types.hpp"

/**
 * @interface IHeartbeatManager
 * @brief Heartbeat generation and monitoring (internal)
 * @internal
 */
class IHeartbeatManager
{
public:
    virtual ~IHeartbeatManager() = default;

    /** @internal */
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

    virtual void tick(uint64_t now_ms) = 0;

    virtual void set_interval_ms(uint32_t heartbeat_interval_ms) = 0;

    /** @internal */
    virtual void handle_response() = 0;

    /** @internal */
    virtual void handle_request(const DecodedPacket &decoded) = 0;
};