// include/statistics_manager.hpp
#pragma once

#include <optional>

#include "i_statistics_manager.hpp"
#include "i_storage_manager.hpp"
#include "i_hal_freertos.hpp"

/**
 * @class StatisticsManager
 * @brief Concrete implementation of IStatisticsManager for tracking peer network metrics.
 *
 * Maintains Exponential Moving Averages (EMA) for RSSI and RTT, along with
 * per-peer packet counters (sent, received, retries, losses, errors).
 *
 * Key design characteristics:
 * - **EMA smoothing**: RSSI uses an alpha value derived from the peer's heartbeat
 *   interval (faster peers get more responsive averages). RTT uses a fixed 12.5%
 *   (1/8) alpha.
 * - **Sentinel values**: An RSSI of @c -127 (@c RSSI_UNKNOWN) indicates "no data
 *   yet". An RTT of @c 0 indicates "no data yet".
 * - **Thread safety**: All public methods acquire a FreeRTOS mutex. The lock timeout
 *   is 5 ms when called from @c rx_task or @c tx_task, and @c portMAX_DELAY when
 *   called from the application thread.
 * - **Persistent storage**: Statistics are flushed to NVS (via IStorageManager) when
 *   per-event dirty counters reach their configured thresholds. This reduces NVS
 *   write frequency while ensuring data survives resets.
 *
 * @note This class is owned by EspNowManager and is not intended for direct
 *       use by application code. Access statistics via the EspNowManager public API.
 *
 * @see IStatisticsManager
 * @see IStorageManager
 */
class StatisticsManager : public IStatisticsManager
{
public:
    /**
     * @brief Constructs a StatisticsManager.
     *
     * @param storage Reference to the storage manager for NVS persistence.
     * @param hal_freertos Reference to the FreeRTOS HAL for mutex and task context detection.
     */
    StatisticsManager(IStorageManager& storage, IFreeRTOSHAL& hal_freertos);
    ~StatisticsManager() override;

    /** @copydoc IStatisticsManager::init */
    esp_err_t init() override;

    /** @copydoc IStatisticsManager::deinit */
    esp_err_t deinit() override;

    /** @copydoc IStatisticsManager::on_peer_added */
    void on_peer_added(NodeId node_id, uint32_t heartbeat_interval_ms) override;

    /** @copydoc IStatisticsManager::on_peer_removed */
    void on_peer_removed(NodeId node_id) override;

    /** @copydoc IStatisticsManager::on_packet_received */
    void on_packet_received(NodeId node_id, int8_t rssi) override;

    /** @copydoc IStatisticsManager::on_ack_received */
    void on_ack_received(NodeId node_id, uint32_t rtt_ms) override;

    /** @copydoc IStatisticsManager::on_delivery_success */
    void on_delivery_success(NodeId node_id) override;

    /** @copydoc IStatisticsManager::on_delivery_failure */
    void on_delivery_failure(NodeId node_id) override;

    /** @copydoc IStatisticsManager::on_driver_error */
    void on_driver_error(NodeId node_id) override;

    /** @copydoc IStatisticsManager::on_packet_lost */
    void on_packet_lost(NodeId node_id) override;

    /** @copydoc IStatisticsManager::on_retry */
    void on_retry(NodeId node_id) override;

    /** @copydoc IStatisticsManager::get */
    bool get(NodeId node_id, PeerStatistics& out) const override;

    /** @copydoc IStatisticsManager::get_all */
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

    bool has_crossed_flush_threshold(const PeerStatisticsEntry& entry) const;
    etl::vector<PeerStatisticsPersist, MAX_PEERS> build_persist_snapshot();
    void reset_dirty_counters();
    std::optional<etl::vector<PeerStatisticsPersist, MAX_PEERS>>
    maybe_build_flush_snapshot(const PeerStatisticsEntry& entry);

    IStorageManager& storage_;
    IFreeRTOSHAL& hal_freertos_;

    etl::vector<PeerStatisticsEntry, MAX_PEERS> entries_;
    SemaphoreHandle_t mutex_ = nullptr;
};
