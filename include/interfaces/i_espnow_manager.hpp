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
     *
     * @code{.cpp}
     * espnow::EspNowConfig config;
     * config.node_id = espnow::ReservedIds::HUB;
     * config.node_type = espnow::ReservedTypes::HUB;
     * config.app_rx_queue = app_queue;
     * config.wifi_channel = 6;
     *
     * esp_err_t err = manager.init(config);
     * if (err != ESP_OK) {
     *     ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(err));
     * }
     * @endcode
     */
    virtual esp_err_t init(const EspNowConfig& config) = 0;

    /**
     * @brief Deinitialize the ESP-NOW Manager
     *
     * Stops all background tasks, releases memory, and deinitializes the ESP-NOW driver.
     *
     * @note Idempotent if is already deinitialized.
     * @note This method does not return errors.
     *
     * @code{.cpp}
     * manager.deinit();
     * @endcode
     */
    virtual void deinit() = 0;

    /**
     * @brief Set the WiFi channel scanning policy.
     *
     * Call with ChannelPolicy::FIXED when the node connects to a WiFi AP so that
     * the discovery manager does not attempt to change the channel.
     * Call with ChannelPolicy::SCAN when the node is not connected to any AP.
     *
     * @param policy ChannelPolicy::SCAN (default) or ChannelPolicy::FIXED
     *
     * @code{.cpp}
     * // When connected to a WiFi AP:
     * manager.set_channel_policy(espnow::ChannelPolicy::FIXED);
     *
     * // If WiFi disconnects:
     * manager.set_channel_policy(espnow::ChannelPolicy::SCAN);
     * @endcode
     */
    virtual void set_channel_policy(ChannelPolicy policy) = 0;

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
     * @param require_ack If true, the calling task blocks until a logical acknowledgment is received or a failure
     * occurs.
     * @return ESP_OK: packet sent successfully (if require_ack=true, logical ACK confirmed by destination node).
     * @return ESP_ERR_INVALID_STATE: manager not in OPERATIONAL state, tx_queue not initialized, or manager stopped
     * during wait.
     * @return ESP_ERR_NOT_FOUND: the peer is not registered in peer storage.
     * @return ESP_ERR_INVALID_ARG: payload length exceeds MAX_PAYLOAD_SIZE.
     * @return ESP_ERR_TIMEOUT: require_ack=true and no logical ACK was received within the maximum retry duration.
     * @return ESP_FAIL: failed to queue message, or maximum physical delivery failures reached (peer unreachable).
     *
     * @note If require_ack=false, this call is non-blocking and returns immediately after queueing.
     * @note If require_ack=true, the calling task blocks for up to (ack_timeout_ms * (logical_ack_retries + 1) + 200)
     * ms.
     * @note Enters `NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures.
     *
     * @warning Maximum payload: 230 bytes (ESP-NOW limit - header - CRC)
     *
     * @code{.cpp}
     * SensorData data = {.temperature = 25.5f, .humidity = 60};
     * esp_err_t err = manager.send_data(
     *     farm::NodeId::HUB,
     *     farm::PayloadType::WATER_LEVEL_REPORT,
     *     &data,
     *     sizeof(data),
     *     true // Require ACK
     * );
     * if (err == ESP_OK) {
     *     ESP_LOGI(TAG, "Data delivered and acknowledged");
     * }
     * @endcode
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
     * @param require_ack If true, the calling task blocks until a logical acknowledgment is received or a failure
     * occurs.
     * @return ESP_OK: command sent successfully (if require_ack=true, logical ACK confirmed by destination node).
     * @return ESP_ERR_INVALID_STATE: manager not in OPERATIONAL state, tx_queue not initialized, or manager stopped
     * during wait.
     * @return ESP_ERR_NOT_FOUND: the peer is not registered in peer storage.
     * @return ESP_ERR_INVALID_ARG: payload length exceeds MAX_PAYLOAD_SIZE.
     * @return ESP_ERR_TIMEOUT: require_ack=true and no logical ACK was received within the maximum retry duration.
     * @return ESP_FAIL: failed to queue message, or maximum physical delivery failures reached (peer unreachable).
     *
     * @note If require_ack=false, this call is non-blocking and returns immediately after queueing.
     * @note If require_ack=true, the calling task blocks for up to (ack_timeout_ms * (logical_ack_retries + 1) + 200)
     * ms.
     * @note Enters `NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures.
     *
     * @warning Maximum payload: 230 bytes (ESP-NOW limit - header - CRC)
     *
     * @code{.cpp}
     * farm::LoadOnCommand cmd{.circuit_id = 0, .power_source = farm::PowerSource::SOLAR, .watchdog_timeout_s = 600};
     * esp_err_t err = manager.send_command(
     *     farm::NodeId::PUMP_CONTROL,
     *     farm::CommandType::LOAD_ON,
     *     &cmd,
     *     sizeof(cmd),
     *     true // Require ACK
     * );
     * @endcode
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
     *
     * @code{.cpp}
     * if (msg.requires_ack) {
     *     manager.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
     * }
     * @endcode
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
     *
     * @code{.cpp}
     * uint8_t pump_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
     * esp_err_t err = manager.add_peer(
     *     farm::NodeId::PUMP_CONTROL,
     *     pump_mac,
     *     farm::NodeType::ACTUATOR,
     *     30000 // 30s heartbeat
     * );
     * @endcode
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
     *
     * @code{.cpp}
     * esp_err_t err = manager.remove_peer(farm::NodeId::PUMP_CONTROL);
     * if (err == ESP_ERR_NOT_FOUND) {
     *     ESP_LOGW(TAG, "Peer was not registered");
     * }
     * @endcode
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
     * @brief Get information for a specific peer.
     *
     * @param node_id The logical ID of the peer.
     * @param out Output parameter populated with peer info if found.
     * @return true if peer was found and out was populated.
     * @return false if peer is not registered or mutex acquisition fails.
     *
     * @code{.cpp}
     * espnow::PeerInfo info{};
     * if (manager.get_peer(farm::NodeId::PUMP_CONTROL, info)) {
     *     ESP_LOGI(TAG, "Heartbeat interval: %lu ms", static_cast<unsigned long>(info.heartbeat_interval_ms));
     * }
     * @endcode
     */
    virtual bool get_peer(NodeId node_id, PeerInfo& out) = 0;

    /**
     * @brief Template overload for get_peer using enum for NodeId
     *
     * @tparam T Enum type for NodeId.
     * @see get_peer() for full documentation
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool get_peer(T node_id, PeerInfo& out)
    {
        return get_peer(static_cast<NodeId>(node_id), out);
    }

    /**
     * @brief Checks if a peer is registered in the peer list.
     *
     * Unlike is_peer_online(), this returns true if the peer is registered/paired
     * regardless of whether it is currently awake or in deep sleep.
     *
     * @param node_id Logical ID of the node to check.
     * @return true if the peer is registered.
     * @return false if the peer is not registered or mutex acquisition fails.
     *
     * @code{.cpp}
     * if (manager.has_peer(farm::NodeId::WEATHER)) {
     *     ESP_LOGI(TAG, "Weather station is registered");
     * }
     * @endcode
     */
    virtual bool has_peer(NodeId node_id) const = 0;

    /**
     * @brief Template overload for has_peer using enum for NodeId
     *
     * @tparam T Enum type for NodeId.
     * @see has_peer() for full documentation
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool has_peer(T node_id) const
    {
        return has_peer(static_cast<NodeId>(node_id));
    }

    /**
     * @brief Get the number of currently registered peers.
     *
     * @return Total count of registered peers.
     *
     * @code{.cpp}
     * size_t count = manager.get_peer_count();
     * ESP_LOGI(TAG, "Active registered peers: %zu", count);
     * @endcode
     */
    virtual size_t get_peer_count() const = 0;

    /**
     * @brief Get a list of all registered peers
     *
     * @return Vector containing information for all registered peers.
     * @note This method does not return errors. Returns empty vector if mutex acquisition fails.
     *
     * @code{.cpp}
     * auto peers = manager.get_peers();
     * for (const auto& peer : peers) {
     *     ESP_LOGI(TAG, "Peer ID: 0x%02X", peer.node_id);
     * }
     * @endcode
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
     *
     * @code{.cpp}
     * espnow::PeerStatistics stats{};
     * if (manager.get_peer_stats(farm::NodeId::WATER_TANK, stats)) {
     *     ESP_LOGI(TAG, "RSSI: %d dBm | Pkt Loss: %.1f%%", stats.last_rssi, stats.packet_loss_percent);
     * }
     * @endcode
     */
    virtual bool get_peer_stats(NodeId node_id, PeerStatistics& out) const = 0;

    /**
     * @brief Template overload for get_peer_stats using enum for NodeId
     *
     * @tparam T Enum type for NodeId.
     * @see get_peer_stats() for full documentation
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool get_peer_stats(T node_id, PeerStatistics& out) const
    {
        return get_peer_stats(static_cast<NodeId>(node_id), out);
    }

    /**
     * @brief Get statistics for all tracked peers.
     *
     * @return Vector of PeerStatistics. Empty if no peers are tracked.
     * @note This method does not return errors.
     *
     * @code{.cpp}
     * auto all_stats = manager.get_all_peer_stats();
     * for (const auto& s : all_stats) {
     *     ESP_LOGI(TAG, "Node 0x%02X: RX %lu, TX %lu", s.node_id, s.packets_rx, s.packets_tx);
     * }
     * @endcode
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
     *
     * @code{.cpp}
     * auto offline = manager.get_offline_peers();
     * for (auto id : offline) {
     *     ESP_LOGW(TAG, "Node 0x%02X is offline", id);
     * }
     * @endcode
     */
    virtual etl::vector<NodeId, MAX_PEERS> get_offline_peers() const = 0;

    /**
     * @brief Checks if a specific peer is currently considered online
     *
     * A peer is considered online if it is registered, has been heard from in the current session,
     * and the elapsed time since its last message does not exceed its configured heartbeat interval
     * multiplied by HEARTBEAT_OFFLINE_MULTIPLIER.
     *
     * @param node_id Logical ID of the node to check.
     * @return true if the peer is registered, active, and within its timeout window.
     * @return false if the peer is unknown, has never sent a message, has no heartbeat interval, or timed out.
     * @note This method does not return errors. Returns false if manager is not operational or pairing.
     *
     * @code{.cpp}
     * if (!manager.is_peer_online(farm::NodeId::PUMP_CONTROL)) {
     *     ESP_LOGW(TAG, "Pump controller unreachable");
     * }
     * @endcode
     */
    virtual bool is_peer_online(NodeId node_id) const = 0;

    /**
     * @brief Template overload for is_peer_online using enum for NodeId
     *
     * @tparam T Enum type for NodeId.
     * @see is_peer_online() for full documentation
     */
    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    bool is_peer_online(T node_id) const
    {
        return is_peer_online(static_cast<NodeId>(node_id));
    }

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
     *
     * @code{.cpp}
     * // Start pairing for 60 seconds:
     * esp_err_t err = manager.start_pairing(60000);
     * @endcode
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
     *
     * @code{.cpp}
     * if (manager.get_node_state() == espnow::NodeState::IDLE) {
     *     manager.reconnect();
     * }
     * @endcode
     */
    virtual esp_err_t reconnect() = 0;

    // ========================================
    // Heartbeat Control
    // ========================================

    /**
     * @brief Enables or disables autonomous heartbeat packet generation at runtime
     *
     * @param enable True to enable autonomous heartbeat sending, false to disable.
     * @note This method does not return errors. Does nothing if manager is uninitialized.
     *
     * @code{.cpp}
     * manager.set_enable_heartbeat(false); // Pause heartbeats before entering sleep
     * @endcode
     */
    virtual void set_enable_heartbeat(bool enable) = 0;

    /**
     * @brief Checks if autonomous heartbeat packet generation is currently enabled
     *
     * @return True if heartbeat generation is enabled and manager is initialized.
     * @note This method does not return errors.
     *
     * @code{.cpp}
     * if (manager.is_heartbeat_enabled()) {
     *     ESP_LOGI(TAG, "Heartbeats active");
     * }
     * @endcode
     */
    virtual bool is_heartbeat_enabled() const = 0;

    /**
     * @brief Sets the heartbeat generation interval in milliseconds at runtime
     *
     * @param interval_ms Interval in milliseconds between autonomous heartbeat packets.
     * @note This method does not return errors. Does nothing if manager is uninitialized.
     *
     * @code{.cpp}
     * manager.set_heartbeat_interval_ms(15000); // Send heartbeat every 15s
     * @endcode
     */
    virtual void set_heartbeat_interval_ms(uint32_t interval_ms) = 0;

    // ========================================
    // Status
    // ========================================

    /**
     * @brief Get the current node state
     *
     * @return The current node state.
     * @note This method does not return errors.
     *
     * @code{.cpp}
     * if (manager.get_node_state() == espnow::NodeState::OPERATIONAL) {
     *     // Link is ready
     * }
     * @endcode
     */
    virtual NodeState get_node_state() const = 0;

    /**
     * @brief Check if EspNowManager is initialized
     *
     * @return true if initialized, false otherwise.
     * @note This method does not return errors.
     *
     * @code{.cpp}
     * if (!manager.is_initialized()) {
     *     manager.init(config);
     * }
     * @endcode
     */
    virtual bool is_initialized() const = 0;
};

} // namespace espnow
