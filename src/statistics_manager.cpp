#include <algorithm>
#include <cstring>

#include "esp_log.h"
#include "statistics_manager.hpp"

// static const char* TAG = "StatsMgr";

StatisticsManager::StatisticsManager(IStorageManager& storage, IFreeRTOSHAL& hal_freertos)
    : storage_(storage)
    , hal_freertos_(hal_freertos)
{
}

StatisticsManager::~StatisticsManager()
{
    // deinit();
}

esp_err_t StatisticsManager::init()
{
    mutex_ = hal_freertos_.mutex_create();
    if (mutex_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    // Load existing statistics from storage if any
    etl::vector<PeerStatisticsPersist, MAX_PEERS> persisted;
    if (storage_.load_stats(persisted) == ESP_OK) {
        for (const auto& p : persisted) {
            PeerStatisticsEntry entry;
            entry.stats.node_id = p.node_id;
            entry.stats.rssi_avg = p.rssi_avg;
            entry.stats.packets_rx = p.packets_rx;
            entry.stats.packets_sent = p.packets_sent;
            entry.stats.packets_lost = p.packets_lost;
            entry.stats.rtt_avg_ms = p.rtt_avg_ms;
            entries_.push_back(entry);
        }
    }

    return ESP_OK;
}

esp_err_t StatisticsManager::deinit()
{
    if (mutex_ != nullptr) {
        // Final flush of all dirty stats
        flush();
        hal_freertos_.semaphore_delete(mutex_);
        mutex_ = nullptr;
    }
    entries_.clear();
    return ESP_OK;
}

void StatisticsManager::on_peer_added(NodeId node_id, uint32_t heartbeat_interval_ms)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry == nullptr) {
            PeerStatisticsEntry new_entry;
            new_entry.stats.node_id = node_id;
            new_entry.stats.rssi_alpha = compute_alpha(heartbeat_interval_ms);
            entries_.push_back(new_entry);
        }
        else {
            entry->stats.rssi_alpha = compute_alpha(heartbeat_interval_ms);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_peer_removed(NodeId node_id)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        auto it = std::find_if(
            entries_.begin(), entries_.end(), [node_id](const auto& e) { return e.stats.node_id == node_id; });
        if (it != entries_.end()) {
            entries_.erase(it);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_packet_received(NodeId node_id, int8_t rssi, int64_t received_at_ms)
{
    (void)received_at_ms; // Not used for now, could be used for jitter calculation
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.rssi_last = rssi;
            if (entry->stats.rssi_avg == 0) {
                entry->stats.rssi_avg = rssi;
            }
            else {
                entry->stats.rssi_avg = update_ema_i8(entry->stats.rssi_avg, rssi, entry->stats.rssi_alpha);
            }
            entry->stats.packets_rx++;
            entry->dirty_rx++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_ack_received(NodeId node_id, uint32_t rtt_ms)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.rtt_last_ms = rtt_ms;
            if (entry->stats.rtt_avg_ms == 0) {
                entry->stats.rtt_avg_ms = rtt_ms;
            }
            else {
                // RTT uses the same alpha as RSSI for now, or a fixed one (e.g. 1/8)
                entry->stats.rtt_avg_ms = update_ema_u32(entry->stats.rtt_avg_ms, rtt_ms, entry->stats.rssi_alpha);
            }
            entry->dirty_rtt++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_delivery_success(NodeId node_id, int64_t sent_at_ms)
{
    (void)sent_at_ms;
    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.packets_sent++;
            entry->dirty_tx++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_delivery_failure(NodeId node_id)
{
    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.delivery_failures++;
            entry->dirty_tx_fail++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_driver_error(NodeId node_id)
{
    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.driver_errors++;
            entry->dirty_driver_err++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_packet_lost(NodeId node_id)
{
    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.packets_lost++;
            entry->dirty_lost++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

void StatisticsManager::on_retry(NodeId node_id)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.retries++;
            // Retries are considered "soft" dirty, no separate flush threshold
            entry->dirty_tx++;
            maybe_flush(*entry);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
}

bool StatisticsManager::get(NodeId node_id, PeerStatistics& out) const
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            out = entry->stats;
            hal_freertos_.semaphore_give(mutex_);
            return true;
        }
        hal_freertos_.semaphore_give(mutex_);
    }
    return false;
}

etl::vector<PeerStatistics, MAX_PEERS> StatisticsManager::get_all() const
{
    etl::vector<PeerStatistics, MAX_PEERS> result;
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
        for (const auto& entry : entries_) {
            result.push_back(entry.stats);
        }
        hal_freertos_.semaphore_give(mutex_);
    }
    return result;
}

// =========================================================================================
// Private methods
// =========================================================================================

StatisticsManager::PeerStatisticsEntry* StatisticsManager::find_entry(NodeId node_id)
{
    for (auto& entry : entries_) {
        if (entry.stats.node_id == node_id) {
            return &entry;
        }
    }
    return nullptr;
}

const StatisticsManager::PeerStatisticsEntry* StatisticsManager::find_entry(NodeId node_id) const
{
    for (const auto& entry : entries_) {
        if (entry.stats.node_id == node_id) {
            return &entry;
        }
    }
    return nullptr;
}

uint8_t StatisticsManager::compute_alpha(uint32_t heartbeat_interval_ms)
{
    // Returns EMA alpha in fixed point (0-256), where 256 is 100% weight to new sample.
    // Short intervals (1s)  -> alpha ~10% (26/256) - Smooth
    // Medium intervals (10s) -> alpha ~20% (51/256)
    // Long intervals (30s)   -> alpha ~25% (64/256)
    // Very long/Unknown      -> alpha ~40% (102/256) - Reactive
    if (heartbeat_interval_ms == 0)
        return 51; // 20%
    if (heartbeat_interval_ms < 5000)
        return 26; // 10%
    if (heartbeat_interval_ms < 30000)
        return 64; // 25%
    return 102;    // 40%
}

int8_t StatisticsManager::update_ema_i8(int8_t avg, int8_t sample, uint8_t alpha)
{
    // EMA = (sample * alpha + avg * (256 - alpha)) / 256
    int32_t a = static_cast<int32_t>(alpha);
    int32_t val = (static_cast<int32_t>(sample) * a + static_cast<int32_t>(avg) * (256 - a)) >> 8;
    return static_cast<int8_t>(val);
}

uint32_t StatisticsManager::update_ema_u32(uint32_t avg, uint32_t sample, uint8_t alpha)
{
    uint32_t a = static_cast<uint32_t>(alpha);
    return (sample * a + avg * (256 - a)) >> 8;
}

void StatisticsManager::maybe_flush(PeerStatisticsEntry& entry)
{
    if (entry.dirty_rx >= flush_threshold_rx_ || entry.dirty_tx >= flush_threshold_tx_ ||
        entry.dirty_tx_fail >= flush_threshold_tx_fail_ || entry.dirty_driver_err >= flush_threshold_driver_err_ ||
        entry.dirty_lost >= flush_threshold_lost_ || entry.dirty_rtt >= flush_threshold_rtt_) {
        flush();
    }
}

void StatisticsManager::flush()
{
    etl::vector<PeerStatisticsPersist, MAX_PEERS> persisted;
    for (const auto& entry : entries_) {
        PeerStatisticsPersist p;
        p.node_id = entry.stats.node_id;
        p.rssi_avg = entry.stats.rssi_avg;
        p.packets_rx = entry.stats.packets_rx;
        p.packets_sent = entry.stats.packets_sent;
        p.driver_errors = entry.stats.driver_errors;
        p.delivery_failures = entry.stats.delivery_failures;
        p.packets_lost = entry.stats.packets_lost;
        p.rtt_avg_ms = entry.stats.rtt_avg_ms;
        persisted.push_back(p);
    }

    if (storage_.store_stats(persisted) == ESP_OK) {
        for (auto& entry : entries_) {
            entry.dirty_rx = 0;
            entry.dirty_tx = 0;
            entry.dirty_tx_fail = 0;
            entry.dirty_driver_err = 0;
            entry.dirty_lost = 0;
            entry.dirty_rtt = 0;
        }

    }
}
