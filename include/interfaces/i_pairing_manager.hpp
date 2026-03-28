// include/internface/i_pairing_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "esp_err.h"

#include "espnow_types.hpp"

/**
 * @interface IPairingManager
 * @brief Pairing logic for connecting nodes to HUB.
 * @internal
 */
class IPairingManager
{
public:
    virtual ~IPairingManager() = default;

    /**
     * @brief Initializes the pairing manager.
     * @param id Node ID.
     * @param type Node type.
     * @param rx_task_handle RX task handle for notifications.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init(NodeId id, NodeType type, TaskHandle_t rx_task_handle) = 0;

    /** @internal */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>

    esp_err_t init(T1 type, T2 id, TaskHandle_t rx_task_handle)
    {
        return init(static_cast<NodeId>(id), static_cast<NodeType>(type), rx_task_handle);
    }

    virtual void tick(uint64_t now_ms) = 0;

    /**
     * @brief Starts the pairing process.
     * @param timeout_ms Timeout in milliseconds.
     * @param now_ms Current time in milliseconds.
     * @return ESP_OK on success.
     */
    virtual esp_err_t start(uint32_t timeout_ms, uint64_t now_ms) = 0;

    /**
     * @brief Handles incoming pair request packets.
     * @param decoded Decoded packet.
     */
    virtual void handle_request(const DecodedPacket& decoded) = 0;

    /**
     * @brief Handles incoming pair response packets.
     * @param decoded Decoded packet.
     */
    virtual void handle_response(const DecodedPacket& decoded) = 0;
};