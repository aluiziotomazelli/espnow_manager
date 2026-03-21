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
    virtual esp_err_t init(uint32_t interval_ms, NodeType type) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeType)>>
    esp_err_t init(uint32_t interval_ms, T type)
    {
        return init(interval_ms, static_cast<NodeType>(type));
    }

    /** @internal */
    virtual void update_node_id(NodeId id) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void update_node_id(T id)
    {
        update_node_id(static_cast<NodeId>(id));
    }

    /** @internal */
    virtual esp_err_t deinit() = 0;
    /** @internal */
    virtual void handle_response(NodeId hub_id) = 0;
    /** @internal */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    void handle_response(T hub_id)
    {
        handle_response(static_cast<NodeId>(hub_id));
    }

    /** @internal */
    virtual void set_channel(uint8_t channel) = 0;

    /** @internal */
    virtual void handle_request(const DecodedPacket &decoded) = 0;
};