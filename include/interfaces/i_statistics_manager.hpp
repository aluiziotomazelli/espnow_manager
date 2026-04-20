// include/interfaces/i_statistics_manager.hpp
#pragma once

#include <cstdint>
#include "etl/vector.h"

#include "espnow_types.hpp"

/**
 * @interface IStatisticsManager
 * @brief Interface for collecting and reporting per-peer network statistics.
 *
 * The StatisticsManager is responsible for tracking link quality metrics for each
 * known peer, including signal strength (RSSI), round-trip time (RTT), packet
 * counters, and error/loss counters. Implementations typically use Exponential
 * Moving Average (EMA) to smooth RSSI and RTT values over time.
 *
 * This interface is called from multiple FreeRTOS tasks:
 * - @c rx_task for reception events (on_packet_received, on_ack_received)
 * - @c tx_task for transmission events (on_delivery_success, on_delivery_failure, etc.)
 * - The application thread for reads (get, get_all)
 *
 * Implementations MUST be thread-safe.
 */
class IStatisticsManager
{
public:
    virtual ~IStatisticsManager() = default;

    // --- Lifecycle ---

    /**
     * @brief Initializes the statistics manager.
     *
     * Allocates required resources (e.g., mutex) and loads any previously
     * persisted statistics from non-volatile storage.
     *
     * @return ESP_OK: on successful initialization
     * @return ESP_ERR_INVALID_STATE: if already initialized
     * @return ESP_ERR_NO_MEM: if resource allocation fails
     * @return ESP_FAIL: on other initialization failure
     */
    virtual esp_err_t init() = 0;

    /**
     * @brief Deinitializes the statistics manager.
     *
     * Flushes any dirty statistics to persistent storage, clears all internal
     * entries, and releases allocated resources.
     *
     * @return ESP_OK: on successful deinitialization
     * @return ESP_ERR_INVALID_STATE: if not initialized
     * @return ESP_FAIL: on deinitialization failure
     */
    virtual esp_err_t deinit() = 0;

    // --- Peer lifecycle events ---

    /**
     * @brief Notifies the manager that a new peer has been added.
     *
     * Creates or updates the internal statistics entry for the given peer.
     * The heartbeat interval is used to derive the EMA smoothing factor (alpha),
     * so that faster-heartbeat peers produce more responsive averages.
     *
     * @param node_id The unique identifier of the peer node.
     * @param heartbeat_interval_ms The peer's heartbeat interval in milliseconds.
     */
    virtual void on_peer_added(NodeId node_id, uint32_t heartbeat_interval_ms) = 0;

    /**
     * @brief Notifies the manager that a peer has been removed.
     *
     * Removes the internal statistics entry for the given peer. Any dirty
     * statistics should be flushed to persistent storage before removal.
     *
     * @param node_id The unique identifier of the peer node.
     */
    virtual void on_peer_removed(NodeId node_id) = 0;

    // --- Rx events (called from rx_task) ---

    /**
     * @brief Records an incoming data or command packet from a peer.
     *
     * Updates the peer's RSSI Exponential Moving Average (EMA) with the
     * measured signal strength and increments the received packet counter.
     * May trigger a flush to persistent storage if a dirty threshold is reached.
     *
     * @param node_id The unique identifier of the peer node that sent the packet.
     * @param rssi The received signal strength indicator in dBm.
     */
    virtual void on_packet_received(NodeId node_id, int8_t rssi) = 0;

    /**
     * @brief Records a logical ACK received from a peer.
     *
     * Updates the peer's RTT Exponential Moving Average (EMA) with the
     * measured round-trip time and triggers a flush if the RTT dirty
     * threshold is reached.
     *
     * @param node_id The unique identifier of the peer node that sent the ACK.
     * @param rtt_ms The round-trip time in milliseconds.
     */
    virtual void on_ack_received(NodeId node_id, uint32_t rtt_ms) = 0;

    // --- Tx delivery events (called from tx_task via callback) ---

    /**
     * @brief Records a successful packet delivery to a peer.
     *
     * Increments the peer's transmitted packet counter.
     *
     * @param node_id The unique identifier of the destination peer node.
     */
    virtual void on_delivery_success(NodeId node_id) = 0;

    /**
     * @brief Records a failed packet delivery to a peer after all retries are exhausted.
     *
     * Increments the peer's delivery failure counter.
     *
     * @param node_id The unique identifier of the destination peer node.
     */
    virtual void on_delivery_failure(NodeId node_id) = 0;

    /**
     * @brief Records a driver-level send error for a peer.
     *
     * Increments the peer's driver error counter and may trigger a flush
     * if the error dirty threshold is reached.
     *
     * @param node_id The unique identifier of the destination peer node.
     */
    virtual void on_driver_error(NodeId node_id) = 0;

    /**
     * @brief Records a packet that was lost after all retransmission attempts were exhausted.
     *
     * Increments the peer's packet loss counter and may trigger a flush
     * if the loss dirty threshold is reached.
     *
     * @param node_id The unique identifier of the destination peer node.
     */
    virtual void on_packet_lost(NodeId node_id) = 0;

    /**
     * @brief Records a retransmission attempt for a peer.
     *
     * Increments the peer's retry counter.
     *
     * @param node_id The unique identifier of the destination peer node.
     */
    virtual void on_retry(NodeId node_id) = 0;

    // --- Reads ---

    /**
     * @brief Retrieves the current statistics for a specific peer.
     *
     * @param node_id The unique identifier of the peer node.
     * @param[out] out Reference to a PeerStatistics struct to be populated.
     * @return true: if statistics for the peer were found and @p out was populated
     * @return false: if no entry exists for the given @p node_id
     */
    virtual bool get(NodeId node_id, PeerStatistics& out) const = 0;

    /**
     * @brief Retrieves statistics for all known peers.
     *
     * @return A vector of PeerStatistics entries, one per known peer.
     *         The vector may be empty if no peers are tracked.
     */
    virtual etl::vector<PeerStatistics, MAX_PEERS> get_all() const = 0;
};