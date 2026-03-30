// include/interfaces/i_discovery_manager.hpp
#pragma once

#include <cstdint>
#include <type_traits>

#include "espnow_types.hpp"

/**
 * @interface IDiscoveryManager
 * @brief WiFi channel scanning and discovery probe handling.
 * @internal
 */
class IDiscoveryManager
{
public:
    virtual ~IDiscoveryManager() = default;

    /**
     * @brief Initialize the discovery manager.
     * @param id This node's ID.
     * @param type This node's type.
     * @param observer Observer for scan events. Can be nullptr.
     * @return ESP_OK on success.
     */
    virtual esp_err_t
    init(NodeId id, NodeType type, TaskHandle_t rx_task_handle, UBaseType_t priority, uint32_t stack_size) = 0;

    /**
     * @brief Deinitialize and stop the discovery task.
     * @note Blocks until task exits (up to 1s timeout).
     */
    virtual void deinit() = 0;

    /**
     * @brief Start an asynchronous channel scan.
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_STATE if scan already in progress or not initialized.
     * @note Non-blocking: signals the internal task to start.
     */
    virtual void start_scan() = 0;

    /**
     * @brief Stop an ongoing scan.
     * @note Non-blocking: signals the internal task to stop.
     */
    virtual void stop_scan() = 0;

    /**
     * @brief Check if a scan is currently in progress.
     * @return true if scanning, false otherwise.
     * @note Thread-safe.
     */
    virtual bool is_scanning() const = 0;

    /**
     * @brief Handle incoming CHANNEL_SCAN_RESPONSE packets.
     * @param decoded The decoded response packet.
     * @note Called from rx_task context. Thread-safe.
     */
    virtual void handle_scan_probe(const DecodedRxPacket& decoded) = 0;

    /**
     * @brief Handle incoming CHANNEL_SCAN_RESPONSE packets.
     * @param decoded The decoded response packet.
     * @note Called from rx_task context. Thread-safe.
     */
    virtual void handle_scan_response(const DecodedRxPacket& decoded) = 0;

    /**
     * @brief Set the WiFi channel for scanning.
     * @param channel Primary channel (1-14).
     * @note Should be called before start_scan().
     */
    virtual void set_channel(uint8_t channel) = 0;

    /**
     * @brief Get the WiFi channel.
     * @return The current WiFi channel.
     */
    virtual uint8_t get_channel() const = 0;

    /** @internal */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t init(T1 id, T2 type, TaskHandle_t rx_task_handle, UBaseType_t priority, uint32_t stack_size)
    {
        return init(static_cast<NodeId>(id), static_cast<NodeType>(type), rx_task_handle, priority, stack_size);
    }
};
