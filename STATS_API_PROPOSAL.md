# Statistics API Proposal — Exposing Peer Stats to the Application

## Problem Statement

The `StatisticsManager` tracks per-peer network quality metrics (RSSI, RTT, packet counts, delivery failures, driver errors) but is entirely internal to `EspNowManager`. The application layer has no way to read these statistics.

Complicating factors:
- **Dual roles**: A Hub manages up to 19 dynamic peers; a Node manages 1 peer (Hub).
- **Automatic pairing**: NodeIds are assigned during pairing — the application does not know them at `init()` time.
- **Dynamic peer lifecycle**: Peers are added/removed automatically; the app discovers them via `get_peers()`.

---

## Design Constraints

| Constraint | Detail |
|-----------|--------|
| **Memory** | 520KB SRAM on ESP32. Stats struct ~52 bytes/peer × 19 = ~988 bytes max. |
| **Thread safety** | Stats protected by FreeRTOS mutex (priority inheritance enabled). TX-side handlers use 5ms timeout; app/ RX-side use `portMAX_DELAY`. |
| **Task priorities** | WiFi task (23) > rx_task (10) > tx_task (9) > app task (3-5). |
| **ESP-IDF convention** | Query APIs with output parameters, not callbacks, for snapshot-style data. |
| **No pre-knowledge of NodeIds** | App discovers peers via `get_peers()` at runtime. |

---

## Options Analysis

### Option A: Query API via EspNowManager (Recommended)

```cpp
// In IEspNowManager / EspNowManager
bool get_peer_stats(NodeId node_id, PeerStatistics& out) const;
etl::vector<PeerStatistics, MAX_PEERS> get_all_peer_stats() const;

// Usage — Node side:
auto peers = mgr.get_peers();
if (peers.size() > 0) {
    PeerStatistics stats;
    if (mgr.get_peer_stats(peers[0].node_id, stats)) {
        printf("Hub RSSI: %d dBm, RTT: %lu ms\n", stats.rssi_avg, (unsigned long)stats.rtt_avg_ms);
    }
}

// Usage — Hub side:
for (const auto& stats : mgr.get_all_peer_stats()) {
    printf("Node %d RSSI: %d dBm\n", stats.node_id, stats.rssi_avg);
}
```

| Aspect | Assessment |
|--------|-----------|
| **Encapsulation** | ✅ Maintained — app interacts only with facade. |
| **ESP-IDF alignment** | ✅ Matches `esp_wifi_get_sta_list()`, `esp_now_get_peer_list()` patterns. |
| **Simplicity** | ✅ Two methods, output parameters, synchronous. |
| **Thread safety** | ✅ Reuses existing mutex. 5-10μs hold time for copy. |
| **App discovers NodeIds** | ✅ Combines with existing `get_peers()`. |
| **Copy cost** | ~1KB per `get_all_peer_stats()` call — negligible on ESP32. |
| **Callback complexity** | ✅ None — app controls when to poll. |

### Option B: Direct StatisticsManager Exposure

```cpp
IStatisticsManager* stats = mgr.get_stats_manager();
PeerStatistics s;
stats->get(node_id, s);
```

| Aspect | Assessment |
|--------|-----------|
| **Encapsulation** | ❌ Broken — app depends on internal component hierarchy. |
| **Flexibility** | ❌ Changing internal architecture breaks app API. |
| **Simplicity** | ✅ Minimal code changes. |
| **Thread safety** | ✅ Same mutex, same safety. |
| **Verdict** | **Not recommended** — violates the facade pattern. |

### Option C: Event-Driven Callbacks

```cpp
struct StatsObserver {
    virtual void on_stats_change(NodeId, const PeerStatistics&) = 0;
};
mgr.register_stats_observer(observer);
```

| Aspect | Assessment |
|--------|-----------|
| **Real-time** | ✅ Immediate notification on every stats change. |
| **Fire rate** | ❌ Every packet changes stats — callback fires hundreds of times/sec. |
| **Deadlock risk** | ⚠️ Callback runs from tx_task/rx_task context. If app blocks, system deadlocks. |
| **Lifecycle management** | ❌ Observer must outlive registration. Dangling pointer risk. |
| **Debouncing** | ❌ App must implement its own throttling/filtering — duplicates internal logic. |
| **Verdict** | **Not recommended** — over-engineered for snapshot data. |

### Option D: Push to app_rx_queue

Insert stats as special `AppMessage` entries when dirty thresholds are crossed.

| Aspect | Assessment |
|--------|-----------|
| **Asynchronous delivery** | ✅ Stats arrive alongside data — no polling needed. |
| **Queue pollution** | ❌ Mixes infrastructure data with application messages. |
| **Volume** | ❌ High — flush happens at every threshold crossing. |
| **Distinguishability** | ❌ App must add type discrimination to `AppMessage`. |
| **Verdict** | **Not recommended** — violates single responsibility of app queue. |

---

## ESP-IDF Established Patterns

ESP-IDF uses **query APIs with output parameters** for snapshot-style data retrieval:

| ESP-IDF API | Pattern |
|-------------|---------|
| `esp_wifi_get_sta_list(wifi_sta_list_t*)` | Query with output param |
| `esp_netif_get_ip_info(esp_netif_t*, esp_netif_ip_info_t*)` | Query with output param |
| `esp_now_get_peer_list(esp_now_peer_list_t*)` | Query with output param |
| `nvs_get_u32(nvs_handle_t*, ...)` | Query with output param |

ESP-IDF reserves **callbacks** for:
- Asynchronous events (WiFi connected, got IP, ESP-NOW recv)
- Streaming data (I2S samples, camera frames)
- Interrupt-driven notifications (GPIO interrupts)

For **peer statistics**, the query pattern is the established convention.

---

## Thread Safety Analysis

### Priority Inversion Risk

When app thread (priority 3) calls `get_peer_stats()` while `rx_task` (priority 10) holds the mutex:

1. App blocks waiting for mutex.
2. FreeRTOS priority inheritance temporarily boosts `rx_task` to app's priority.
3. Critical section completes in ~5-10μs (vector iteration + memcpy).
4. App unblocks immediately.

**Risk: Negligible.** The hold time is orders of magnitude below the FreeRTOS tick period (10ms). Even if preempted, priority inheritance ensures bounded blocking.

### Copy Cost

| Metric | Value |
|--------|-------|
| Per-peer struct size | ~52 bytes |
| Max peers | 19 |
| Total copy size | ~988 bytes |
| CPU time (ESP32 @ 240MHz) | ~5-10μs |
| SRAM impact | 0.2% of 520KB |

**Acceptable.** The ESP32 handles this easily. Document: *"Call at reasonable intervals (≥1s) to avoid contention."*

### TX-Path Contention

TX-side handlers (`on_delivery_success`, `on_delivery_failure`, etc.) use `pdMS_TO_TICKES(5)` timeout. If the app holds the mutex during an NVS flush, TX stats updates are silently skipped — no blocking, no deadlock.

---

## Recommendation

**Implement Option A** — Query API via `EspNowManager`:

```cpp
// Single peer query
bool get_peer_stats(NodeId node_id, PeerStatistics& out) const;

// All peers at once
etl::vector<PeerStatistics, MAX_PEERS> get_all_peer_stats() const;
```

### Rationale

1. Aligns with ESP-IDF conventions
2. Maintains facade encapsulation
3. Zero callback/deadlock risk
4. App discovers NodeIds via existing `get_peers()`
5. Thread-safe via existing mutex
6. Minimal implementation (~10 lines of delegation code)

### Future Enhancement: Combined Status Struct

A convenience method that merges `PeerInfo` + `PeerStatistics` + online status:

```cpp
struct PeerStatus {
    PeerInfo info;           // node_id, mac, type, last_seen, paired
    PeerStatistics stats;    // RSSI, RTT, counters
    bool is_online;          // Derived: last_seen within heartbeat window
};

etl::vector<PeerStatus, MAX_PEERS> get_all_peer_status() const;
```

This enables a single call for a "peer dashboard" — ~1.6KB copy, still well within budget.

---

## Files to Modify (when approved)

| File | Change |
|------|--------|
| `include/interfaces/i_espnow_manager.hpp` | Add `get_peer_stats()` and `get_all_peer_stats()` declarations |
| `include/espnow_manager.hpp` | Add method declarations |
| `src/espnow_manager.cpp` | Implement: delegate to `stats_mgr_->get()` and `stats_mgr_->get_all()` |
| `host_test/test_espnow_manager/` | Add tests for both methods |
