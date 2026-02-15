#pragma once

#include <type_traits>
#include <vector>

#include "esp_err.h"

#include "espnow_types.hpp"

// ========================================
// ESP-NOW Manager Interface
// ========================================

/**
 * @interface IEspNowManager
 * @brief Interface for the ESP-NOW Manager, providing high-level APIs for communication, peer management, and lifecycle.
 *
 * This interface defines the contract for managing ESP-NOW communications in a structured way,
 * supporting both HUB (central controller) and NODE (peripheral) roles.
 *
 * @author Jules
 * @version 1.0.0
 * @date 2025
 * @see EspNow for implementation details
 * @see espnow_types.hpp for data structures
 */
class IEspNowManager
{
public:
    virtual ~IEspNowManager() = default;

    // ========================================
    // Lifecycle
    // ========================================

    /**
     * @brief Initialize the ESP-NOW Manager
     *
     * Sets up the necessary resources, including WiFi, ESP-NOW drivers, tasks, and queues.
     * For HUB: Prepares to receive data from multiple nodes and manage the peer list.
     * For NODE: Prepares to communicate with the HUB and optionally starts heartbeats.
     *
     * @param config Configuration structure containing node ID, type, and resource settings.
     * @return ESP_OK on success, or an error code from the underlying ESP-IDF drivers.
     * @note This method must be called before any other operation.
     */
    virtual esp_err_t init(const EspNowConfig &config) = 0;

    /**
     * @brief Deinitialize the ESP-NOW Manager
     *
     * Stops all background tasks, releases memory, and deinitializes the ESP-NOW driver.
     *
     * @return ESP_OK on success, or an error code if deinitialization fails.
     */
    virtual esp_err_t deinit() = 0;

    // ========================================
    // Data Communication
    // ========================================

    /**
     * @brief Send data to a destination node
     *
     * Encapsulates the payload into a standard message format and queues it for transmission.
     * For HUB: Used to send application data to a specific registered node.
     * For NODE: Typically used to send sensor data or status updates to the HUB.
     *
     * @param dest_node_id ID of the destination node.
     * @param payload_type Type identifier for the payload (application-defined).
     * @param payload Pointer to the data buffer to be sent.
     * @param len Length of the payload in bytes.
     * @param require_ack If true, the transmission will wait for a logical acknowledgment.
     * @return ESP_OK if the packet was successfully queued, ESP_ERR_NOT_FOUND if the peer is not registered.
     */
    virtual esp_err_t send_data(NodeId dest_node_id,
                                PayloadType payload_type,
                                const void *payload,
                                size_t len,
                                bool require_ack = false) = 0;

    /**
     * @brief Template overload for send_data using enum types
     *
     * @tparam T1 Enum type for NodeId.
     * @tparam T2 Enum type for PayloadType.
     * @param dest_node_id Destination node ID.
     * @param payload_type Payload type.
     * @param payload Pointer to the data.
     * @param len Data length.
     * @param require_ack Logical ACK request.
     * @return esp_err_t result.
     */
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(PayloadType)>>
    esp_err_t send_data(T1 dest_node_id, T2 payload_type, const void *payload, size_t len, bool require_ack = false)
    {
        return send_data(static_cast<NodeId>(dest_node_id), static_cast<PayloadType>(payload_type), payload, len,
                         require_ack);
    }

    /**
     * @brief Send a command to a destination node
     *
     * Similar to send_data, but specifically for control commands.
     * For HUB: Used to control node behavior (e.g., change reporting interval).
     * For NODE: Can be used to request actions from the HUB.
     *
     * @param dest_node_id ID of the destination node.
     * @param command_type Type of command to execute.
     * @param payload Optional payload for the command.
     * @param len Length of the payload.
     * @param require_ack If true, waits for a logical acknowledgment.
     * @return ESP_OK on success, or error code.
     */
    virtual esp_err_t send_command(NodeId dest_node_id,
                                   CommandType command_type,
                                   const void *payload,
                                   size_t len,
                                   bool require_ack = false) = 0;

    /**
     * @brief Template overload for send_command using enum for NodeId
     *
     * @tparam T Enum type for NodeId.
     * @param dest_node_id Destination node ID.
     * @param command_type Command type.
     * @param payload Command payload.
     * @param len Payload length.
     * @param require_ack Logical ACK request.
     * @return esp_err_t result.
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t send_command(T dest_node_id,
                           CommandType command_type,
                           const void *payload,
                           size_t len,
                           bool require_ack = false)
    {
        return send_command(static_cast<NodeId>(dest_node_id), command_type, payload, len, require_ack);
    }

    /**
     * @brief Confirm the reception of a message that required an ACK
     *
     * Sends a logical acknowledgment back to the sender of the last received message that had the `require_ack` flag set.
     * This should be called by the application after successfully processing the received data.
     *
     * @param status Status of the processing (e.g., SUCCESS or FAILURE).
     * @return ESP_OK if ACK was sent, ESP_ERR_INVALID_STATE if no message is pending ACK.
     */
    virtual esp_err_t confirm_reception(AckStatus status) = 0;

    // ========================================
    // Peer Management
    // ========================================

    /**
     * @brief Manually add a peer to the manager
     *
     * Registers a node in the internal peer list and adds it to the ESP-NOW driver's peer table.
     *
     * @param node_id Unique ID of the node.
     * @param mac MAC address of the node (6 bytes).
     * @param channel WiFi channel the node is operating on.
     * @param type Role/Type of the node.
     * @return ESP_OK on success, or error if peer table is full.
     */
    virtual esp_err_t add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type) = 0;

    /**
     * @brief Template overload for add_peer using enums
     *
     * @tparam T1 Enum type for NodeId.
     * @tparam T2 Enum type for NodeType.
     * @param node_id Node ID.
     * @param mac MAC address.
     * @param channel WiFi channel.
     * @param type Node type.
     * @return esp_err_t result.
     */
    template <typename T1,
              typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add_peer(T1 node_id, const uint8_t *mac, uint8_t channel, T2 type)
    {
        return add_peer(static_cast<NodeId>(node_id), mac, channel, static_cast<NodeType>(type));
    }

    /**
     * @brief Remove a peer from the manager
     *
     * Removes the peer from both internal lists and the ESP-NOW driver.
     *
     * @param node_id ID of the node to remove.
     * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not present.
     */
    virtual esp_err_t remove_peer(NodeId node_id) = 0;

    /**
     * @brief Template overload for remove_peer using enum
     *
     * @tparam T Enum type for NodeId.
     * @param node_id Node ID.
     * @return esp_err_t result.
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t remove_peer(T node_id)
    {
        return remove_peer(static_cast<NodeId>(node_id));
    }

    /**
     * @brief Get a list of all registered peers
     *
     * @return Vector containing information for all registered peers.
     */
    virtual std::vector<PeerInfo> get_peers() = 0;

    /**
     * @brief Get a list of IDs for peers considered offline
     *
     * A peer is considered offline if no heartbeat has been received within its expected interval.
     *
     * @return Vector of Node IDs.
     */
    virtual std::vector<NodeId> get_offline_peers() const = 0;

    // ========================================
    // Pairing
    // ========================================

    /**
     * @brief Start the pairing process
     *
     * For HUB: Enters a mode where it accepts pairing requests from new nodes.
     * For NODE: Starts broadcasting pairing requests to find a HUB.
     *
     * @param timeout_ms Duration of the pairing mode in milliseconds.
     * @return ESP_OK if pairing started successfully.
     */
    virtual esp_err_t start_pairing(uint32_t timeout_ms = 30000) = 0;

    // ========================================
    // Status
    // ========================================

    /**
     * @brief Check if the manager is initialized
     *
     * @return true if initialized, false otherwise.
     */
    virtual bool is_initialized() const = 0;
};
