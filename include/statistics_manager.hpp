// include/statistics_manager.hpp
#pragma once

#include "i_statistics_manager.hpp"
#include "i_storage_manager.hpp"
#include "i_hal_freertos.hpp"

/**
 * @class StatisticsManager
 * @brief Concrete implementation of IStatisticsManager for tracking peer network metrics.
 *
 * Maintains Exponential Moving Averages (EMA) for RSSI and RTT, along with packet counters.
 * It flushes statistics to persistent storage when event-specific thresholds are reached.
 */
class StatisticsManager : public IStatisticsManager
{
public:
    StatisticsManager(IStorageManager& storage, IFreeRTOSHAL& hal_freertos);
    ~StatisticsManager() override;

    esp_err_t init() override;
    esp_err_t deinit() override;

    void on_peer_added(NodeId node_id, uint32_t heartbeat_interval_ms) override;
    void on_peer_removed(NodeId node_id) override;

    void on_packet_received(NodeId node_id, int8_t rssi, int64_t received_at_ms) override;
    void on_ack_received(NodeId node_id, uint32_t rtt_ms) override;

    void on_delivery_success(NodeId node_id, int64_t sent_at_ms) override;
    void on_delivery_failure(NodeId node_id) override;
    void on_driver_error(NodeId node_id) override;
    void on_packet_lost(NodeId node_id) override;
    void on_retry(NodeId node_id) override;

    bool get(NodeId node_id, PeerStatistics& out) const override;
    etl::vector<PeerStatistics, MAX_PEERS> get_all() const override;

private:
    struct PeerStatisticsEntry
    {
        PeerStatistics stats;
        uint8_t dirty_rx = 0;
        uint8_t dirty_tx = 0;
        uint8_t dirty_tx_fail = 0;
        uint8_t dirty_driver_err = 0;
        uint8_t dirty_lost = 0;
        uint8_t dirty_rtt = 0;
    };

    static constexpr uint8_t flush_threshold_rx_ = FLUSH_THRESHOLD_RX;
    static constexpr uint8_t flush_threshold_tx_ = FLUSH_THRESHOLD_TX;
    static constexpr uint8_t flush_threshold_tx_fail_ = FLUSH_THRESHOLD_TX_FAILURE;
    static constexpr uint8_t flush_threshold_driver_err_ = FLUSH_THRESHOLD_TX_FAILURE;
    static constexpr uint8_t flush_threshold_lost_ = FLUSH_THRESHOLD_LOSS;
    static constexpr uint8_t flush_threshold_rtt_ = FLUSH_THRESHOLD_RTT;

    static uint8_t compute_alpha(uint32_t heartbeat_interval_ms);
    static int8_t update_ema_i8(int8_t avg, int8_t sample, uint8_t alpha);
    static uint32_t update_ema_u32(uint32_t avg, uint32_t sample, uint8_t alpha);

    PeerStatisticsEntry* find_entry(NodeId node_id);
    const PeerStatisticsEntry* find_entry(NodeId node_id) const;

    void maybe_flush(PeerStatisticsEntry& entry);
    void flush();

    IStorageManager& storage_;
    IFreeRTOSHAL& hal_freertos_;

    etl::vector<PeerStatisticsEntry, MAX_PEERS> entries_;
    SemaphoreHandle_t mutex_ = nullptr;
};
