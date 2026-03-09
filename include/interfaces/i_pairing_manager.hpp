// include/internface/i_pairing_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"

#include "espnow_types.hpp"

/**
 * @interface IPairingManager
 * @brief Pairing logic for connecting nodes to HUB (internal)
 * @internal
 */
class IPairingManager
{
public:
    virtual ~IPairingManager() = default;

    /** @internal */
    virtual esp_err_t init(NodeType type, NodeId id) = 0;

    /** @internal */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeType)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeId)>>

    esp_err_t init(T1 type, T2 id)
    {
        return init(static_cast<NodeType>(type), static_cast<NodeId>(id));
    }

    /** @internal */
    virtual esp_err_t deinit() = 0;

    /** @internal */
    virtual esp_err_t start(uint32_t timeout_ms) = 0;

    /** @internal */
    virtual bool is_active() const = 0;

    /** @internal */
    virtual void handle_request(const RxPacket &packet) = 0;

    /** @internal */
    virtual void handle_response(const RxPacket &packet) = 0;
};