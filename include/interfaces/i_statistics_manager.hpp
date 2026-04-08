// include/internface/i_statistics_manager.hpp
#pragma once

#include <cstdint>
#include "etl/vector.h"

#include "espnow_types.hpp"

/**
 * @interface IStatisticsManager
 * @brief Interface for statistics management.
 * @internal
 */
class IStatisticsManager
{
public:
    virtual ~IStatisticsManager() = default;

    // --- Lifecycle ---
    virtual esp_err_t init() = 0;
    virtual esp_err_t deinit() = 0;

    // --- Peer lifecycle events ---
    virtual void on_peer_added(NodeId node_id, uint32_t heartbeat_interval_ms) = 0;
    virtual void on_peer_removed(NodeId node_id) = 0;

    // --- Rx events (called from rx_task) ---
    virtual void on_packet_received(NodeId node_id, int8_t rssi, uint64_t received_at_ms) = 0;
    virtual void on_ack_received(NodeId node_id, uint32_t rtt_ms) = 0;

    // --- Tx events (called from tx_task) ---
    virtual void on_packet_sent(NodeId node_id, uint64_t sent_at_ms) = 0;
    virtual void on_packet_lost(NodeId node_id) = 0;
    virtual void on_retry(NodeId node_id) = 0;

    // --- Reads ---
    virtual bool get(NodeId node_id, PeerStatistics& out) const = 0;
    virtual etl::vector<PeerStatistics, MAX_PEERS> get_all() const = 0;
};