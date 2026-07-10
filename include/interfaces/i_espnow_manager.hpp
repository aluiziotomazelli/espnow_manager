#pragma once

#include <type_traits>

#include "etl/vector.h"

#include "esp_err.h"

#include "espnow_types.hpp"
namespace espnow {

// ========================================
// ESP-NOW Manager Interface
// ========================================

/**
 * @interface IEspNowManager
 * @brief Interface for the ESP-NOW Manager, providing high-level APIs for communication, peer management, and
 * lifecycle.
 *
 * This interface defines the contract for managing ESP-NOW communications in a structured way,
 * supporting both HUB (central controller) and NODE (peripheral) roles.
 *
 * @author [github.com/aluiziotomazelli]
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
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_STATE: already initialized or wifi_set_mode == WIFI_MODE_NULL
     * @return ESP_ERR_INVALID_ARG: app_rx_queue is null (not initialized and passed as argument)
     * @return ESP_ERR_NO_MEM: memory allocation for mutex and internal queues fails
     * @return ESP_FAIL: failed to create internal tasks
     * @return Other: internal esp_now.h or esp_wifi.h errors
     *
     * @note This method must be called before any other operation.
     * @note On fail, deinit() is called automatically.
     */
    virtual esp_err_t init(const EspNowConfig& config) = 0;

    /**
     * @brief Deinitialize the ESP-NOW Manager
     *
     * Stops all background tasks, releases memory, and deinitializes the ESP-NOW driver.
     *
     * @note Idempotent if is already deinitialized.
     * @note This method does not return errors.
     */
    virtual void deinit() = 0;

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
     * @return ESP_OK: the packet was successfully queued.
     * @return ESP_ERR_INVALID_STATE: manager not in OPERATIONAL state or tx_queue not initialized.
     * @return ESP_ERR_NOT_FOUND: the peer is not registered.
     * @return ESP_ERR_INVALID_ARG: payload length exceeds MAX_PAYLOAD_SIZE.
     * @return ESP_FAIL: failed to send message to tx_queue_.
     *
     * @note Non-blocking unless require_ack=true
     * @note Enters `NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures
     *
     * @warning Maximum payload: 230 bytes (ESP-NOW limit - header - CRC)
     */
    virtual esp_err_t send_data(
        NodeId dest_node_id,
        PayloadType payload_type,
        const void* payload,
        size_t len,
        bool require_ack = false) = 0;

    /**
     * @brief Template overload for send_data using enum types
     *
     * @tparam T1 Enum type for NodeId.
     * @tparam T2 Enum type for PayloadType.
     * @see send_data() for full documentation
     */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(PayloadType)>>
    esp_err_t send_data(T1 dest_node_id, T2 payload_type, const void* payload, size_t len, bool require_ack = false)
    {
        return send_data(
            static_cast<NodeId>(dest_node_id), static_cast<PayloadType>(payload_type), payload, len, require_ack);
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
     * @return ESP_OK: on success.
     * @return ESP_ERR_INVALID_STATE: manager not in OPERATIONAL state or tx_queue not initialized.
     * @return ESP_ERR_NOT_FOUND: the peer is not registered.
     * @return ESP_ERR_INVALID_ARG: payload length exceeds MAX_PAYLOAD_SIZE.
     * @return ESP_FAIL: failed to send message to tx_queue_.
     *
     * @note Non-blocking unless require_ack=true
     * @note Enters `NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures
     *
     * @warning Maximum payload: 230 bytes (ESP-NOW limit - header - CRC)
     */
    virtual esp_err_t send_command(
        NodeId dest_node_id,
        CommandType command_type,
        const void* payload,
        size_t len,
        bool require_ack = false) = 0;

    /**
     * @brief Template overload for send_command using enum for NodeId
     *
     * @tparam T Enum type for NodeId.
     * @see send_command() for full documentation
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t
    send_command(T dest_node_id, CommandType command_type, const void* payload, size_t len, bool require_ack = false)
    {
        return send_command(static_cast<NodeId>(dest_node_id), command_type, payload, len, require_ack);
    }

    /**
     * @brief Confirm the reception of a message that required an ACK
     *
     * Sends a logical acknowledgment back to the specified sender. This should be called by the
     * application after processing a received message that had the `require_ack` flag set, to inform
     * the sender of the processing outcome.
     *
     * @param sender_id Logical ID of the sender node to acknowledge.
     * @param sequence_number Sequence number of the original message being acknowledged.
     * @param status Processing outcome: `AckStatus::OK` for success, `AckStatus::ERROR_INVALID_DATA`
     *               for invalid payload, or `AckStatus::ERROR_PROCESSING` for internal errors.
     * @return ESP_OK: ACK was queued successfully.
     * @return ESP_ERR_INVALID_STATE: manager not in OPERATIONAL/PAIRING state, or tx_queue not initialized.
     * @return ESP_ERR_NOT_FOUND: peer MAC not found for the specified sender_id.
     * @return ESP_FAIL: failed to queue ACK packet for transmission.
     */
    virtual esp_err_t confirm_reception(NodeId sender_id, uint16_t sequence_number, AckStatus status) = 0;

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
     * @param heartbeat_interval_ms Heartbeat interval in milliseconds.
     * @return ESP_OK: on success.
     * @return ESP_ERR_INVALID_ARG: mac is nullptr.
     * @return ESP_ERR_TIMEOUT: failed to acquire mutex within timeout.
     * @return ESP_ERR_NO_MEM: ESP-NOW driver failed to allocate memory for peer.
     * @return ESP_ERR_ESPNOW_NOT_INIT: ESP-NOW driver not initialized.
     * @return ESP_ERR_ESPNOW_ARG: invalid argument passed to ESP-NOW driver.
     * @return ESP_ERR_ESPNOW_NO_MEM: ESP-NOW driver out of memory.
     * @return ESP_ERR_ESPNOW_NOT_FOUND: peer not found when updating existing peer.
     * @return ESP_ERR_ESPNOW_CHAN: invalid WiFi channel.
     * @return ESP_ERR_ESPNOW_IF: invalid interface.
     * @return ESP_ERR_WIFI_NOT_INIT: WiFi not initialized.
     * @return ESP_ERR_WIFI_NOT_STARTED: WiFi not started.
     * @return ESP_ERR_WIFI_ARG: invalid WiFi argument.
     * @return ESP_ERR_INVALID_STATE: storage failed to persist peer data.
     *
     * @note List uses LRU (Least Recently Used) policy with maximum MAX_PEERS = 19 (ESP-NOW limitation)
     * @note When full, oldest peer (least recently used) is removed to make room
     * @note Re-adding existing peer moves it to front (marks as recently used)
     * @note Automatically persisted to RTC and NVS storage if is a new peer or readding one with different mac or wifi
     * channel
     *
     * @warning ESP-NOW hardware limit is 20 peers, but 1 is reserved for broadcast
     */
    virtual esp_err_t add_peer(NodeId node_id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms) = 0;

    /**
     * @brief Template overload for add_peer using enums
     *
     * @tparam T1 Enum type for NodeId.
     * @tparam T2 Enum type for NodeType.
     * @see add_peer() for full documentation
     */
    template <
        typename T1,
        typename T2,
        typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
        typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add_peer(T1 node_id, const uint8_t* mac, T2 type, uint32_t heartbeat_interval_ms)
    {
        return add_peer(static_cast<NodeId>(node_id), mac, static_cast<NodeType>(type), heartbeat_interval_ms);
    }

    /**
     * @brief Remove a peer from the manager
     *
     * Removes the peer from both internal lists and the ESP-NOW driver.
     *
     * @param node_id ID of the node to remove.
     * @return ESP_OK: on success.
     * @return ESP_ERR_NOT_FOUND: the peer is not present.
     * @return ESP_ERR_TIMEOUT: failed to acquire mutex within timeout.
     * @return ESP_ERR_ESPNOW_NOT_INIT: ESP-NOW driver not initialized.
     * @return ESP_ERR_ESPNOW_ARG: invalid argument passed to ESP-NOW driver.
     * @return ESP_ERR_ESPNOW_NOT_FOUND: peer not found in ESP-NOW driver.
     * @return ESP_ERR_ESPNOW_CHAN: invalid WiFi channel.
     * @return ESP_ERR_ESPNOW_IF: invalid interface.
     * @return ESP_ERR_WIFI_NOT_INIT: WiFi not initialized.
     * @return ESP_ERR_WIFI_NOT_STARTED: WiFi not started.
     * @return ESP_ERR_WIFI_ARG: invalid WiFi argument.
     * @return ESP_ERR_INVALID_STATE: storage failed to persist peer removal.
     */
    virtual esp_err_t remove_peer(NodeId node_id) = 0;

    /**
     * @brief Template overload for remove_peer using enum
     *
     * @tparam T Enum type for NodeId.
     * @see remove_peer() for full documentation
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
     * @note This method does not return errors. Returns empty vector if mutex acquisition fails.
     */
    virtual etl::vector<PeerInfo, MAX_PEERS> get_peers() = 0;

    // ========================================
    // Statistics
    // ========================================

    /**
     * @brief Get statistics for a specific peer.
     *
     * @param node_id The logical ID of the peer.
     * @param out Output parameter filled with current statistics.
     * @return true if the peer was found and out was populated.
     * @return false if the peer is not tracked or stats not yet available.
     */
    virtual bool get_peer_stats(NodeId node_id, PeerStatistics& out) const = 0;

    /**
     * @brief Get statistics for all tracked peers.
     *
     * @return Vector of PeerStatistics. Empty if no peers are tracked.
     * @note This method does not return errors.
     */
    virtual etl::vector<PeerStatistics, MAX_PEERS> get_all_peer_stats() const = 0;

    /**
     * @brief Get a list of IDs for peers considered offline
     *
     * A peer is considered offline if no heartbeat has been received within its
     * expected interval multiplied by HEARTBEAT_OFFLINE_MULTIPLIER.
     *
     * @return Vector of Node IDs. Returns empty vector if mutex acquisition fails or manager not operational.
     * @note This method does not return errors.
     * @see HEARTBEAT_OFFLINE_MULTIPLIER in protocol_types.hpp
     */
    virtual etl::vector<NodeId, MAX_PEERS> get_offline_peers() const = 0;

    // ========================================
    // Pairing
    // ========================================

    /**
     * @brief Start the pairing process
     *
     * **For HUB:**
     * - Enters listening mode for pairing requests
     * - Accepts requests from any node broadcasting
     * - Automatically adds responding peers to peer list
     *
     * **For NODE:**
     * - Broadcasts pairing request periodically (every 1s)
     * - Waits for HUB to respond with acknowledgment
     * - Automatically stops when paired or timeout reached
     *
     * @param timeout_ms Duration of the pairing mode in milliseconds.
     * @return ESP_OK: pairing started successfully.
     * @return ESP_ERR_INVALID_STATE: manager is UNINITIALIZED or pairing already active.
     *
     * @note Automatic stop after specified timeout duration
     *
     * @warning Both HUB and NODE must be in pairing mode simultaneously
     */
    virtual esp_err_t start_pairing(uint32_t timeout_ms = 30000) = 0;

    /**
     * @brief Attempt to reconnect after scan exhaustion
     *
     * Resets the retry counter and immediately triggers a RECOVERY_SCAN.
     * Intended to be called by the application when the node is in IDLE
     * state due to exhausted scan retries. Semantically different from
     * start_pairing(): reconnect assumes the HUB ID is already and in the
     * peer list and the node just needs to find the HUB channel.
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_STATE if node is not in IDLE state
     * @return ESP_ERR_INVALID_ARG if there are no peers
     */
    virtual esp_err_t reconnect() = 0;

    // ========================================
    // Status
    // ========================================

    /**
     * @brief Get the current node state
     *
     * @return The current node state.
     * @note This method does not return errors.
     */
    virtual NodeState get_node_state() const = 0;

    /**
     * @brief Check if EspNowManager is initialized
     *
     * @return true if initialized, false otherwise.
     * @note This method does not return errors.
     */
    virtual bool is_initialized() const = 0;

    /**
     * @brief Get the application RX queue handle.
     * @return QueueHandle_t or nullptr if not initialized.
     */
    virtual QueueHandle_t get_rx_queue() const = 0;
};

} // namespace espnow
