# Delivery Tracking Cleanup

Four improvements to eliminate spurious `LOGW "Delivery event for unknown MAC"` messages,
improve protocol correctness during pairing, and reduce mutex contention from NVS writes.

---

## Change 0 — PAIR_RESPONSE via broadcast + `dest_node_id` filter in `handle_response`

**Motivation:** Currently the HUB sends PAIR_RESPONSE as a **unicast** to the node's MAC
(`decoded.raw.src_mac`). This requires `peer_mgr_.add()` to be called *before* `queue_packet()`
— because the ESP-NOW driver rejects sends to unregistered peers — which means the peer exists
in the driver but not yet in the logical peer list when `send_cb` fires. This is the root cause
of the pairing LOGW.

Sending PAIR_RESPONSE as **broadcast** eliminates this entirely: the driver needs no registered
peer for broadcast, so `peer_mgr_.add()` on the HUB side can still happen before the send (or
be deferred), and the `send_cb` is silenced by the broadcast filter in Change 1.

The node side must add a `dest_node_id == my_id_` check in `handle_response` to correctly
discard responses intended for other nodes, which is critical when multiple nodes are pairing
simultaneously.

### [MODIFY] [pairing_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/pairing_manager.cpp)

#### `handle_request` — send response to broadcast MAC

Replace `memcpy(tx_packet.dest_mac, decoded.raw.src_mac, 6)` (line 104) with broadcast:

```cpp
memcpy(tx_packet.dest_mac, BROADCAST_MAC, 6);
```

everything else in `handle_request` stays the same — `peer_mgr_.add()` still happens before
`queue_packet()` so the HUB knows the peer before the next heartbeat arrives.

#### `handle_response` — filter by `dest_node_id`

Add a node-identity check before accepting the response (line 147):

```cpp
void PairingManager::handle_response(const DecodedRxPacket& decoded)
{
    if (!is_initialized_ || !is_active_)
        return;
    if (my_type_ == ReservedTypes::HUB)
        return;

    // Broadcast responses must be explicitly addressed to this node.
    // Without this check, two nodes pairing simultaneously would both
    // accept the first ACCEPTED response on air.
    if (decoded.header.dest_node_id != my_id_)
        return;

    const PairResponse* resp = reinterpret_cast<const PairResponse*>(decoded.raw.data);
    if (resp->status == PairStatus::ACCEPTED) {
        ESP_LOGI(TAG, "Pairing accepted by Hub");
        peer_mgr_.add(decoded.header.sender_node_id, decoded.raw.src_mac, decoded.header.sender_type);
        is_active_ = false;
        notify_rx_task_pairing_done();
    }
}
```

**Effect:** After this change, every send from the pairing flow is either broadcast (PAIR_REQUEST,
PAIR_RESPONSE) or unicast to a fully registered peer (post-pairing traffic). The pairing LOGW
is eliminated at the protocol level.

---

## Change 1 — Filter broadcast MAC in `esp_now_send_cb`

**Motivation:** All broadcast sends (PAIR_REQUEST, PAIR_RESPONSE after Change 0, CHANNEL_SCAN_PROBE,
CHANNEL_SCAN_RESPONSE) fire the send callback but have no logical peer counterpart in the peer
list. Their delivery tracking is meaningless: no ACK is expected, and pre-send driver errors are
already accounted for in `handle_esp_now_send_errors`. Forwarding these events to `notify_delivery`
causes unnecessary task wake-ups and the LOGW for scanning flows.

### [MODIFY] [espnow_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/espnow_manager.cpp)

Add a broadcast MAC check at the top of `esp_now_send_cb` (line 496), before calling
`notify_delivery`. If `des_addr` is the broadcast address (`FF:FF:FF:FF:FF:FF`), return early.

```cpp
void EspNowManager::esp_now_send_cb(const esp_now_send_info_t* info, esp_now_send_status_t status)
{
    EspNowManager* self = s_active_instance_;
    if (self == nullptr || info == nullptr || info->des_addr == nullptr)
        return;

    // Broadcast sends have no logical peer counterpart. Delivery tracking is meaningless
    // for them. Driver-level errors are already handled synchronously via
    // handle_esp_now_send_errors() right after hal_esp_now_send() returns.
    static constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    if (memcmp(info->des_addr, kBroadcastMac, 6) == 0)
        return;

    self->tx_manager_->notify_delivery(status, info->des_addr);
}
```

**Effect:** After Changes 0 and 1, every send that reaches `notify_delivery` is unicast to a
fully registered peer. Any `find_node_id_by_mac` that returns `NOT_FOUND` at that point is a
genuine bug, not an expected protocol condition.

---

## Change 2 — `find_node_id_by_mac` return type: `bool` → `esp_err_t`

**Motivation:** The current `bool` return collapses two distinct failure modes into a single
`false`. After Changes 0 and 1, there is **no legitimate path** where `find_node_id_by_mac`
returns `NOT_FOUND` for a delivery event — every send that reaches `notify_delivery` is unicast
to a fully registered peer. A `NOT_FOUND` at that point is a real bug signal (memory corruption,
peer evicted incorrectly, logic error) and must **not** be silenced.

The `esp_err_t` return separates two distinct failure modes with different severity:

| Condition | Current | Proposed | Action in caller |
|---|---|---|---|
| MAC found | `true` | `ESP_OK` | proceed normally |
| MAC not in peer list | `false` | `ESP_ERR_NOT_FOUND` | **LOGW — genuine bug** |
| Mutex timeout | `false` | `ESP_ERR_TIMEOUT` | LOGW — transient contention |

### [MODIFY] [i_peer_manager.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/interfaces/i_peer_manager.hpp)

Update the virtual declaration and its Doxygen comment (lines 126–132):

```cpp
/**
 * @brief Looks up node ID by MAC address.
 * @param mac 6-byte MAC address to search for.
 * @param out_id Output parameter for the found node ID. Unchanged on failure.
 * @return ESP_OK              MAC found; out_id is populated.
 * @return ESP_ERR_NOT_FOUND  MAC is not in the peer list (unexpected — indicates a bug).
 * @return ESP_ERR_TIMEOUT    Could not acquire the mutex within the deadline.
 */
virtual esp_err_t find_node_id_by_mac(const uint8_t* mac, NodeId& out_id) = 0;
```

### [MODIFY] [peer_manager.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/peer_manager.hpp)

Update the override declaration and its `@copydoc`:

```cpp
/** @copydoc IPeerManager::find_node_id_by_mac */
esp_err_t find_node_id_by_mac(const uint8_t* mac, NodeId& out_id) override;
```

### [MODIFY] [peer_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/peer_manager.cpp)

Rewrite `find_node_id_by_mac` (lines 169–184):

```cpp
esp_err_t PeerManager::find_node_id_by_mac(const uint8_t* mac, NodeId& out_id)
{
    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(10)) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    esp_err_t ret = ESP_ERR_NOT_FOUND;
    for (const auto& p : peers_) {
        if (memcmp(p.mac, mac, 6) == 0) {
            out_id = p.node_id;
            ret = ESP_OK;
            break;
        }
    }

    hal_freertos_.semaphore_give(mutex_);
    return ret;
}
```

### [MODIFY] [tx_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/tx_manager.cpp)

Update both call sites in `handle_notifications` (lines 364–393). Both error cases now log,
but with different messages to differentiate transient contention from real bugs:

```cpp
// NOTIFY_DELIVERY_FAILURE block
while (freertos_hal_.queue_receive(delivery_queue_, &event, 0) == pdTRUE) {
    NodeId node_id = 0;
    esp_err_t err = peer_mgr_.find_node_id_by_mac(event.dest_mac, node_id);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Delivery event: mutex timeout resolving MAC");
        continue;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Delivery event for unregistered MAC — unexpected after protocol fix");
        continue;
    }
    stats_mgr_.on_delivery_failure(node_id);
}

// NOTIFY_DELIVERY_SUCCESS block (same pattern)
while (freertos_hal_.queue_receive(delivery_queue_, &event, 0) == pdTRUE) {
    NodeId node_id = 0;
    esp_err_t err = peer_mgr_.find_node_id_by_mac(event.dest_mac, node_id);
    if (err == ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Delivery event: mutex timeout resolving MAC");
        continue;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Delivery event for unregistered MAC — unexpected after protocol fix");
        continue;
    }
    stats_mgr_.on_delivery_success(node_id);
}
```

> [!NOTE]
> The existing `TODO` comment on line 388 can be removed after this change.

**Mocks and tests:** The `MockPeerManager` return type must be updated from `bool` to `esp_err_t`.
All test cases that set expectations on `find_node_id_by_mac` must be updated accordingly.

---

## Change 3 — Release mutex before NVS write in `PeerManager` and `StatisticsManager`

**Motivation:** Both `PeerManager::add/remove` and `StatisticsManager::flush` currently hold their
mutex while writing to NVS, which can block for tens of milliseconds. This directly causes
`find_node_id_by_mac` (10 ms timeout) to spuriously return `ESP_ERR_TIMEOUT` during pairing or
scanning, which is the root cause of the remaining mutex-timeout warnings.

The fix is the **copy-then-release** pattern: snapshot the data under the mutex, release it, then
call NVS.

### [MODIFY] [peer_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/peer_manager.cpp)

#### `PeerManager::add` (lines 38–74)

Extract the peers snapshot and release the mutex before calling `storage_.store_peers`:

```cpp
esp_err_t PeerManager::add(NodeId id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms)
{
    if (mac == nullptr)
        return ESP_ERR_INVALID_ARG;

    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    PeerInfo* existing_by_id  = find_peer_by_id(id);
    PeerInfo* existing_by_mac = find_peer_by_mac(mac);

    esp_err_t ret = ESP_OK;
    if (existing_by_id != nullptr)
        ret = update_existing_peer_by_id(existing_by_id, mac, type, heartbeat_interval_ms);
    else if (existing_by_mac != nullptr)
        reassign_mac_to_new_id(existing_by_mac, id, type, heartbeat_interval_ms);
    else
        ret = add_new_peer_to_empty_slot(id, mac, type, heartbeat_interval_ms);

    // Snapshot under mutex, then release before NVS write.
    etl::vector<PersistentPeer, MAX_PEERS> snapshot;
    if (ret == ESP_OK) {
        for (const auto& p : peers_)
            snapshot.push_back(info_to_persistent(p));
    }
    hal_freertos_.semaphore_give(mutex_);

    if (ret == ESP_OK)
        ret = storage_.store_peers(snapshot, true);

    return ret;
}
```

#### `PeerManager::remove` (lines 76–98)

Same pattern — release mutex before `save_peers_to_storage`:

```cpp
esp_err_t PeerManager::remove(NodeId id)
{
    if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) != pdTRUE)
        return ESP_ERR_TIMEOUT;

    auto it = std::find_if(peers_.begin(), peers_.end(),
                           [id](const PeerInfo& p) { return p.node_id == id; });
    if (it == peers_.end()) {
        hal_freertos_.semaphore_give(mutex_);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = hal_espnow_.hal_esp_now_del_peer(it->mac);
    etl::vector<PersistentPeer, MAX_PEERS> snapshot;
    if (ret == ESP_OK) {
        peers_.erase(it);
        for (const auto& p : peers_)
            snapshot.push_back(info_to_persistent(p));
    }
    hal_freertos_.semaphore_give(mutex_);

    if (ret == ESP_OK)
        ret = storage_.store_peers(snapshot, true);

    return ret;
}
```

> [!IMPORTANT]
> `save_peers_to_storage()` must be **deleted**: declaration removed from `peer_manager.hpp` and
> definition removed from `peer_manager.cpp`. Both `add` and `remove` now build and own their
> snapshots inline.

### [MODIFY] [statistics_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/statistics_manager.cpp)

`flush()` is always called from `maybe_flush()`, which itself runs under `mutex_` (held by each
`on_*` caller). The chosen approach is to **return the snapshot to the caller** so the caller
releases the mutex, calls `storage_.store_stats`, and only resets dirty counters on success.

#### Data-safety rule (matching current `flush()` semantics)

Dirty counters are reset **only after a successful NVS write**. If `store_stats` fails, the
counters remain dirty and the next event will trigger another flush attempt. This preserves the
"at-least-once" guarantee of the current implementation.

#### Bug fix — missing `retries` field in current `flush()`

`on_retry()` increments `entry.stats.retries` but the current `flush()` never copies it to the
persistent struct — it is silently dropped on every NVS write. Fix this in
`build_persist_snapshot()` by adding `p.retries = entry.stats.retries`. Also verify that
`PeerStatisticsPersist` declares the field and that `StatisticsManager::init()` loads it back.

#### Private helpers introduced

```cpp
// Returns true if any dirty counter in `entry` has reached its flush threshold.
// Extracted from the inline condition in maybe_flush(). Caller must hold mutex_.
bool StatisticsManager::has_crossed_flush_threshold(const PeerStatisticsEntry& entry) const;

// Builds a full snapshot of all entries_ for persistence.
// Bug fix: now includes retries field. No side-effects on dirty counters.
// Caller must hold mutex_.
etl::vector<PeerStatisticsPersist, MAX_PEERS>
StatisticsManager::build_persist_snapshot();

// Resets all dirty counters across all entries.
// Caller must hold mutex_.
void StatisticsManager::reset_dirty_counters();

// Returns a full snapshot of all entries_ if `entry` has crossed any flush threshold
// (checked via has_crossed_flush_threshold). Returns empty optional otherwise.
// No side-effects on dirty counters. Caller must hold mutex_.
std::optional<etl::vector<PeerStatisticsPersist, MAX_PEERS>>
StatisticsManager::maybe_build_flush_snapshot(const PeerStatisticsEntry& entry);
```

The existing `maybe_flush(entry)` and `flush()` private methods are **deleted** and replaced
by the helpers above.

#### Pattern for each `on_*` method

Two mutex acquisitions per flush: first to update stats and build the snapshot, second (brief,
only on flush) to reset dirty counters after a confirmed successful NVS write.

```cpp
void StatisticsManager::on_delivery_success(NodeId node_id)  // representative example
{
    std::optional<etl::vector<PeerStatisticsPersist, MAX_PEERS>> snapshot;

    if (hal_freertos_.semaphore_take(mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
        auto entry = find_entry(node_id);
        if (entry != nullptr) {
            entry->stats.packets_sent++;
            entry->dirty_tx++;
            snapshot = maybe_build_flush_snapshot(*entry); // snapshot only, no reset
        }
        hal_freertos_.semaphore_give(mutex_);  // released before NVS
    }

    if (snapshot.has_value()) {
        if (storage_.store_stats(snapshot.value()) == ESP_OK) {
            // Reset only on confirmed write — preserves at-least-once guarantee.
            // Edge case: a concurrent on_* may have incremented a dirty counter between
            // snapshot capture (above) and this reset. That dirty increment is lost, but
            // the underlying stat was already updated in memory and will re-dirty on the
            // next event. No actual statistical data is lost.
            if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
                reset_dirty_counters();
                hal_freertos_.semaphore_give(mutex_);
            }
        }
    }
}
```

The same pattern applies to all other `on_*` methods: `on_packet_received`, `on_ack_received`,
`on_delivery_failure`, `on_driver_error`, `on_packet_lost`, `on_retry`.

#### `deinit()` update

Replace the existing implementation entirely. `entries_.clear()` happens inside the mutex before
the NVS write. If `store_stats` subsequently fails, the in-memory data is already cleared — this
is acceptable during teardown. No `reset_dirty_counters()` call needed since the vector is gone.

```cpp
esp_err_t StatisticsManager::deinit()
{
    if (mutex_ != nullptr) {
        std::optional<etl::vector<PeerStatisticsPersist, MAX_PEERS>> snapshot;

        if (hal_freertos_.semaphore_take(mutex_, portMAX_DELAY) == pdTRUE) {
            snapshot = build_persist_snapshot();  // capture before clearing
            entries_.clear();
            hal_freertos_.semaphore_give(mutex_);
        }

        if (snapshot.has_value()) {
            storage_.store_stats(snapshot.value());
        }

        hal_freertos_.semaphore_delete(mutex_);
        mutex_ = nullptr;
    }
    return ESP_OK;
}
```

---

## Summary of files touched

| File | Change |
|---|---|
| `src/espnow_manager.cpp` | Filter broadcast MAC in `esp_now_send_cb` |
| `include/interfaces/i_peer_manager.hpp` | `find_node_id_by_mac` return `bool` → `esp_err_t` |
| `include/peer_manager.hpp` | Override declaration updated |
| `src/peer_manager.cpp` | Implementation + `add`/`remove` mutex-before-NVS |
| `src/tx_manager.cpp` | Call sites updated to branch on `esp_err_t` |
| `src/statistics_manager.cpp` | `maybe_flush`/`flush` two-phase NVS write |
| `host_test/common/mock_peer_manager.hpp` | *(update mock return type — not detailed here)* |
| Host test cases using `find_node_id_by_mac` | *(update expectations — not detailed here)* |

---

## Verification

- Build `test_apps/build_test` with `idf.py build` — confirms no compile errors.
- Run affected host tests to confirm mocks and expectations align with new signatures.
- On-target: flash HUB and NODE, run pairing and scanning scenarios, confirm no `LOGW "Delivery
  event for unknown MAC"` appears in scanning flows; confirm the pairing LOGW is now a clean
  silent skip (no LOGW) since `ESP_ERR_NOT_FOUND` is handled without logging.
