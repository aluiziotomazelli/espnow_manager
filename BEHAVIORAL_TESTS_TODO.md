# Behavioral Tests TODO - ESP-NOW Manager

This document lists missing behavioral tests for the `espnow_manager` component, ranked by importance and feasibility.

---

## Summary

| Priority | Count | Host-Testable | On-Target Required |
|----------|-------|---------------|-------------------|
| Critical | 3 | ✅ 3 | - |
| High | 3 | ✅ 3 | - |
| Medium | 3 | - | ✅ 3 |
| Low | 3 | ✅ 3 | - |

**Total:** 12 tests identified  
**Host-Testable:** 9 (75%)  
**Requires Hardware:** 3 (25%)

---

## 🔴 Critical Priority (Host Test Possible)

### 1. ACK Timeout and Retry Flow

**File:** `host_test/test_espnow_manager/main/test_espnow_manager_task.cpp`

**Why Critical:** Core reliability mechanism. Verifies the retry logic and state transition to SCANNING after MAX_FAILURES.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTaskTest, AckTimeoutTriggersRetryAndScanning)
{
    init_operational_sut();
    
    uint8_t payload[] = {0x01, 0x02};
    
    // Send data requiring ACK
    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, payload, 2, true), ESP_OK);
    
    // Don't call confirm_reception() - simulate timeout
    // TxManager should retry MAX_FAILURES times then enter SCANNING
    vTaskDelay(pdMS_TO_TICKS(LOGICAL_ACK_TIMEOUT_MS * MAX_FAILURES + 100));
    
    // Verify state transition to SCANNING
    EXPECT_EQ(sut_->get_node_state(), NodeState::SCANNING);
}
```

**Implementation Notes:**
- Mock `tx_mgr_->notify_physical_fail()` to simulate transmission failures
- Verify `tx_fsm_` transitions through RETRYING states
- Final state should be SCANNING after `MAX_FAILURES` (3)

---

### 2. State Transition: SCANNING → OPERATIONAL

**File:** `host_test/test_espnow_manager/main/test_espnow_manager_task.cpp`

**Why Critical:** Verifies the recovery path when a node rediscovers its HUB after losing connection.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTaskTest, ScanningFindsChannelTransitionsToOperational)
{
    init_sut();
    
    // Force SCANNING state
    sut_->set_node_state(NodeState::SCANNING);
    ASSERT_EQ(sut_->get_node_state(), NodeState::SCANNING);
    
    // Simulate channel found callback from DiscoveryManager
    sut_->on_channel_found_cb(6);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
    
    // Should transition to OPERATIONAL (has peers in storage)
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
    
    // Verify channel was persisted
    EXPECT_CALL(*storage_, store_channel(6)).Times(1);
}
```

**Implementation Notes:**
- Use `set_node_state_operational()` helper then manually set SCANNING
- May need to add `set_node_state()` to `EspNowManagerTestable`
- Verify `storage_->store_channel()` is called

---

### 3. Full Non-HUB Pairing Flow

**File:** `host_test/test_espnow_manager/main/test_espnow_manager_task.cpp`

**Why Critical:** Complete pairing workflow for sensor/peripheral nodes. Most complex state machine path.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTaskTest, NonHubFullPairingFlowScanToOperational)
{
    // Initialize as non-HUB (SENSOR node)
    EspNowConfig cfg = make_valid_config();
    cfg.node_type = 0x02;  // SENSOR type
    cfg.node_id = 0x05;
    init_sut_with_config(cfg);
    
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);
    
    // Start pairing process
    EXPECT_EQ(sut_->start_pairing(30000), ESP_OK);
    
    // Non-HUB should trigger scanning first
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    
    // Simulate scan success - channel found
    sut_->on_channel_found_cb(6);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
    
    // Simulate receiving PAIR_RESPONSE
    // (Inject packet via rx_queue_handle_)
    
    // Should transition to OPERATIONAL after successful pairing
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
}
```

**Implementation Notes:**
- Mock `pairing_manager_->is_active()` to return false after timeout
- Inject synthetic PAIR_RESPONSE packet via `rx_queue_handle_`
- Verify peer was added via `peer_manager_->add()`

---

## 🟡 High Priority (Host Test Possible)

### 4. Concurrent Operations

**File:** `host_test/test_espnow_manager/main/test_espnow_manager_task.cpp`

**Why High:** Validates thread safety of the rx_task while main thread sends data.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTaskTest, ConcurrentSendAndReceive)
{
    init_operational_sut();
    
    // Create sender task
    auto sender_task = [](void *arg) {
        EspNowManagerTestable *sut = (EspNowManagerTestable *)arg;
        for (int i = 0; i < 10; i++) {
            uint8_t payload[10];
            sut->send_data(kHubId, kPayloadType, payload, 10, false);
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        vTaskDelete(NULL);
    };
    
    xTaskCreate(sender_task, "sender", 2048, sut_.get(), 5, NULL);
    
    // Simultaneously inject RX packets
    for (int i = 0; i < 10; i++) {
        receive_valid_rx_packet();
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Verify no crashes, all packets processed
    EXPECT_TRUE(sut_->is_initialized());
}
```

---

### 5. Heartbeat Timeout → Offline Peer Detection

**File:** `host_test/test_espnow_manager/main/test_espnow_manager.cpp`

**Why High:** Critical for link health monitoring. Tests `get_offline_peers()` functionality.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTest, HeartbeatTimeoutMarksPeerOffline)
{
    init_operational_sut();
    
    // Add peer with 1s heartbeat interval
    PeerInfo peer{};
    peer.node_id = kHubId;
    peer.heartbeat_interval_ms = 1000;
    peer.last_seen_ms = 0;
    
    // Mock peer manager to return this peer
    etl::vector<PeerInfo, MAX_PEERS> peers;
    peers.push_back(peer);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));
    
    // Mock timer to simulate time passage
    ON_CALL(*hal_timer_, get_time_us())
        .WillByDefault(Return(4000000));  // 4 seconds later
    
    // 4s > 3x heartbeat interval (3s), peer should be offline
    auto offline = sut_->get_offline_peers();
    
    EXPECT_EQ(offline.size(), 1);
    EXPECT_EQ(offline[0], kHubId);
}
```

---

### 6. Re-init After Deinit (Full Resource Check)

**File:** `host_test/test_espnow_manager/main/test_espnow_manager.cpp`

**Why High:** Ensures cleanup and re-initialization works correctly.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTest, ReinitAfterDeinitRecreatesResources)
{
    init_sut();
    ASSERT_TRUE(sut_->is_initialized());
    
    // Deinit
    sut_->deinit();
    ASSERT_FALSE(sut_->is_initialized());
    
    // Re-init
    EspNowConfig cfg = make_valid_config();
    EXPECT_EQ(sut_->init(cfg), ESP_OK);
    EXPECT_TRUE(sut_->is_initialized());
    
    // Verify resources recreated
    EXPECT_NE(sut_->ack_mutex_, nullptr);
    EXPECT_NE(sut_->rx_queue_handle_, nullptr);
    EXPECT_NE(sut_->rx_task_handle_, nullptr);
}
```

---

## 🟠 Medium Priority (Requires On-Target)

### 7. Real ESP-NOW Transmission + ACK

**File:** `test_apps/test_ack_flow/main/test_ack.c`

**Why Medium:** Requires actual ESP-NOW radio, physical devices.

**Test Setup:**
- 2x ESP32 devices (1 HUB, 1 NODE)
- NODE sends data with `require_ack=true`
- HUB calls `confirm_reception(AckStatus::OK)`
- Verify NODE receives ACK and doesn't retry

**Hardware Required:** ✅ Yes

---

### 8. RSSI-based Link Quality

**File:** `test_apps/test_rssi_monitor/main/test_rssi.c`

**Why Medium:** Needs real signal strength measurements.

**Test Setup:**
- Measure RSSI at various distances
- Verify `RxPacket::rssi` field is populated correctly
- Test link quality thresholds

**Hardware Required:** ✅ Yes

---

### 9. Channel Interference/Scan Real Behavior

**File:** `test_apps/test_channel_scan/main/test_scan.c`

**Why Medium:** Requires actual WiFi channel scanning in real environment.

**Test Setup:**
- Create interference on specific channels
- Verify `DiscoveryManager` finds clean channel
- Test scan timeout behavior

**Hardware Required:** ✅ Yes

---

## 🟢 Low Priority (Host Test Possible)

### 10. Queue Full Log Message

**File:** `host_test/test_espnow_manager/main/test_espnow_manager_task.cpp`

**Why Low:** Already functionally covered. Log verification is nice-to-have.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTaskTest, LogsWarningWhenQueueFull)
{
    init_and_wait();
    
    // Fill queue and send one more
    // Verify ESP_LOGW is called with "App queue full"
    // Requires mock logger
}
```

---

### 11. Multiple Peers LRU Eviction

**File:** `host_test/test_espnow_manager/main/test_espnow_manager.cpp`

**Why Low:** Edge case for MAX_PEERS limit.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTest, LruEvictionWhenMaxPeersReached)
{
    init_operational_sut();
    
    // Add 19 peers (MAX_PEERS)
    for (uint8_t i = 1; i <= 19; i++) {
        uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, i};
        sut_->add_peer(i, mac, 0x02, 1000);
    }
    
    // Add 20th peer - should evict oldest (peer 1)
    uint8_t new_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 20};
    sut_->add_peer(20, new_mac, 0x02, 1000);
    
    // Verify peer 1 was evicted
    // Verify peer 20 was added
}
```

---

### 12. Storage Persistence Across "Reboot"

**File:** `host_test/test_espnow_manager/main/test_espnow_manager.cpp`

**Why Low:** Mock-based test of persistence logic.

**Test Outline:**
```cpp
TEST_F(EspNowManagerTest, StorageSyncsRtcToNvs)
{
    // Init and add peer
    init_sut();
    sut_->add_peer(kHubId, test_mac, 0x02, 1000);
    
    // Verify RTC and NVS both updated
    EXPECT_CALL(*storage_, store_channel(_)).Times(1);
    
    // Simulate "reboot" - create new instance
    auto sut2 = create_fresh_instance();
    sut2->init(cfg);
    
    // Verify peer loaded from storage
    auto peers = sut2->get_peers();
    EXPECT_EQ(peers.size(), 1);
    EXPECT_EQ(peers[0].node_id, kHubId);
}
```

---

## Implementation Roadmap

### Phase 1: Immediate (Week 1)
**Focus:** Critical reliability tests

```bash
# Files to modify
host_test/test_espnow_manager/main/test_espnow_manager_task.cpp

# Tests to add
1. AckTimeoutTriggersRetryAndScanning
2. ScanningFindsChannelTransitionsToOperational
3. NonHubFullPairingFlowScanToOperational
```

### Phase 2: Secondary (Week 2)
**Focus:** High-priority edge cases

```bash
# Files to modify
host_test/test_espnow_manager/main/test_espnow_manager.cpp
host_test/test_espnow_manager/main/test_espnow_manager_task.cpp

# Tests to add
4. ConcurrentSendAndReceive
5. HeartbeatTimeoutMarksPeerOffline
6. ReinitAfterDeinitRecreatesResources
7. LruEvictionWhenMaxPeersReached
```

### Phase 3: Integration (Week 3-4)
**Focus:** On-device hardware tests

```bash
# New test apps to create
test_apps/test_ack_flow/
test_apps/test_rssi_monitor/
test_apps/test_channel_scan/

# Requires: 2+ ESP32 devices, WiFi environment
```

---

## Test Coverage Impact

| Phase | Tests | Estimated Coverage Gain |
|-------|-------|------------------------|
| Phase 1 | 3 | +5-8% (critical paths) |
| Phase 2 | 4 | +3-5% (edge cases) |
| Phase 3 | 3 | +2-3% (integration) |
| **Total** | **10** | **+10-16%** |

**Current Coverage:** 97.5% lines, 80.8% branches  
**Target Coverage:** 98%+ lines, 90%+ branches

---

## Notes

- **Host tests** run on Linux with mocked HALs - fast execution, CI-friendly
- **On-target tests** require ESP32 hardware and real ESP-NOW communication
- Priority order based on:
  1. Impact on reliability
  2. Likelihood of bugs
  3. Coverage gap size
  4. Implementation complexity

---

**Last Updated:** 2026-03-25  
**Author:** AI Code Review Assistant
