# ESP-NOW Manager - Behavior Test Report

This document lists the behavior tests that should be implemented to fully cover the `EspNowManager` functionality, organized by category and priority.

---

## Table of Contents

1. [Host-Based Tests (no real tasks)](#1-host-based-tests-no-real-tasks---test_espnow_managercpp)
2. [Host-Based Tests (with real tasks)](#2-host-based-tests-with-real-tasks---test_espnow_manager_taskcpp)
3. [On-Device Tests (real hardware required)](#3-on-device-tests-real-hardware-required)
4. [Priority Summary](#priority-summary)

---

## 1. Host-Based Tests (no real tasks) - `test_espnow_manager.cpp`

**Status: 87 tests implemented ✅**

### 1.1 Initialization & Configuration

| Test | Description | Status |
|-------|-----------|--------|
| `InitLoadsChannelFromStorage` | Verify that channel loaded from storage is used on initialization | ✅ Done |
| `InitStoresChannelAtEnd` | Channel must be persisted at end of init | ✅ Done |
| `InitCallsEspNowDriverInit` | Verify ESP-NOW driver is initialized | ✅ Done |
| `InitCallsQueueCreate` | Verify internal queues are created | ✅ Done |
| `InitCallsTaskCreate` | Verify internal tasks are created | ✅ Done |
| `InitCallsTxManagerInit` | TxManager init called | ✅ Done |
| `InitCallsDiscoveryManagerInit` | DiscoveryManager init called | ✅ Done |
| `InitCallsPairingManagerInit` | PairingManager init called | ✅ Done |
| `InitCallsChannelMonitorInit` | ChannelMonitor init called | ✅ Done |
| `InitCallsStatsManagerInit` | StatsManager init called | ✅ Done |
| `InitReturnsFailIfFirstQueueCreationFails` | Queue creation failure handling | ✅ Done |
| `InitReturnsFailIfFirstTaskCreationFails` | Task creation failure handling | ✅ Done |
| `InitReturnsFailIfEspNowDriverInitFails` | Driver init failure handling | ✅ Done |
| `InitReturnsFailIfDiscoveryManagerInitFails` | DiscoveryManager init failure | ✅ Done |
| `InitReturnsFailIfPairingManagerInitFails` | PairingManager init failure | ✅ Done |
| `InitReturnsFailIfChannelMonitorInitFails` | ChannelMonitor init failure | ✅ Done |
| `InitReturnsFailIfTxManagerInitFails` | TxManager init failure | ✅ Done |
| `InitReturnsFailIfStatsManagerInitFails` | StatsManager init failure | ✅ Done |
| `InitReturnsInvalidArgIfAppQueueIsNull` | Null app queue validation | ✅ Done |
| `InitReturnsInvalidStateIfAlreadyInitialized` | Double init guard | ✅ Done |
| `InitPropagatesCorrectNodeIdAndTypeToPairingManager` | Config propagation | ✅ Done |
| `InitPropagatesCorrectIntervalAndTypeToHeartbeatManager` | Config propagation | ✅ Done |
| `InitWithNoPeersTransitionsToPairingScan` | No peers → PAIRING_SCAN | ✅ Done |
| `InitWithPeersTransitionsToOperational` | With peers → OPERATIONAL | ✅ Done |
| `InitCallsDiscoveryManagerInitWhenNodeIsHub` | HUB-specific discovery init | ✅ Done |

### 1.2 Deinitialization

| Test | Description | Status |
|-------|-----------|--------|
| `DeinitDoesNotCleanResourcesWhenNotInitialized` | Safe deinit without init | ✅ Done |
| `DeinitDoesNotDeleteNullTaskHandles` | Null handle handling | ✅ Done |
| `DeinitCallsAllDeleteFunctions` | All resources deleted | ✅ Done |
| `DeinitTransitionsToUninitialized` | Final state is UNINITIALIZED | ✅ Done |
| `DeinitCallsTxManagerDeinit` | TxManager deinit called | ✅ Done |
| `DeinitCallsEspNowDriverDeinit` | Driver deinit called | ✅ Done |
| `DeinitCallsStatsManagerDeinit` | StatsManager deinit called | ✅ Done |
| `DeinitWithPeersCallsDeletePeers` | Peers removed on deinit | ✅ Done |
| `ReinitAfterDeinitSucceeds` | Re-initialization works | ✅ Done |

### 1.3 Pairing

| Test | Description | Status |
|-------|-----------|--------|
| `StartPairingWithPeersTransitionsToPairingAndCallsPairingManagerStart` | HUB+peers → PAIRING | ✅ Done |
| `StartPairingNotOperationalReturnsInvalidState` | State guard | ✅ Done |
| `StartPairingWithoutPeersTransitionsToPairingScan` | No peers → PAIRING_SCAN | ✅ Done |
| `StartPairingForHubCallsPairingManagerStart` | HUB pairing start | ✅ Done |
| `StartPairingWhileScanningCallsStopScan` | Scan stopped before pairing | ✅ Done |

### 1.4 Data Transmission

| Test | Description | Status |
|-------|-----------|--------|
| `SendNonOperationalStateReturnsInvalidState` | State guard | ✅ Done |
| `SendToNonExistentPeerReturnsNotFound` | Peer not found | ✅ Done |
| `SendToExistentPeerCallsQueuePacket` | Valid send → queue | ✅ Done |
| `FailureToQueuePacketReturnsFail` | Queue failure handling | ✅ Done |
| `SendDataWithPayloadCopiesData` | Payload copied correctly | ✅ Done |
| `SendDataWithoutPayloadSendsHeaderOnly` | Header-only packet | ✅ Done |
| `SendDataWithOversizedPayloadReturnsInvalidArg` | Oversized payload guard | ✅ Done |

### 1.5 Reception Confirmation (ACK)

| Test | Description | Status |
|-------|-----------|--------|
| `ConfirmReceptionNonOperationalStateReturnsInvalidState` | State guard | ✅ Done |
| `ConfirmReceptionNonExistentPeerReturnsNotFound` | Unknown peer | ✅ Done |
| `ConfirmReceptionEnqueueFailureReturnsFail` | Queue failure | ✅ Done |
| `ConfirmReceptionSuccess` | Normal ACK flow | ✅ Done |
| `ConfirmReceptionResetsHeaderWhenPeerNotFound` | Header cleanup | ✅ Done |

### 1.6 Peer Management

| Test | Description | Status |
|-------|-----------|--------|
| `AddPeerCallsPeerManagerAdd` | Delegates to peer_mgr | ✅ Done |
| `AddPeerCallsOnPeerAdded` | Stats notified | ✅ Done |
| `AddPeerDoesNotCallOnPeerAddedWhenFails` | Stats not notified on failure | ✅ Done |
| `AddPeerReturnsPeerManagerFailure` | Peer add failure propagation | ✅ Done |
| `RemovePeerCallsPeerManagerRemove` | Delegates to peer_mgr | ✅ Done |
| `RemovePeerCallsOnPeerRemoved` | Stats notified | ✅ Done |
| `RemovePeerDoesNotCallOnPeerRemovedWhenFails` | Stats not notified on failure | ✅ Done |
| `RemovePeerReturnsPeerManagerFailure` | Peer remove failure propagation | ✅ Done |
| `GetPeersCallsPeerManagerGetAll` | Delegates to peer_mgr | ✅ Done |
| `GetOfflinePeersCallsPeerManagerGetOffline` | Delegates to peer_mgr | ✅ Done |
| `GetOfflinePeersNotOperationalReturnsEmptyVector` | Non-operational guard | ✅ Done |

### 1.7 Statistics (NEW since original report)

| Test | Description | Status |
|-------|-----------|--------|
| `GetPeerStatsCallsStatsManagerGet` | Delegates to stats_mgr | ✅ Done |
| `GetPeerStatsReturnsFalseWhenNotFound` | Unknown peer → false | ✅ Done |
| `GetAllPeerStatsCallsStatsManagerGetAll` | Delegates to stats_mgr | ✅ Done |

### 1.8 Notifications & State Transitions

| Test | Description | Status |
|-------|-----------|--------|
| `NotifyMaxFailuresCallsOnScanRequested` | MAX_FAILURES → scan | ✅ Done |
| `NotifyMaxFailuresCallsStartScan` | Scan started | ✅ Done |
| `NotifyChannelFoundCallsScannerGetChannel` | Channel retrieved | ✅ Done |
| `NotifyChannelFoundCallsOnChannelFound` | State transition | ✅ Done |
| `NotifyChannelFoundCallsStopScan` | Scan stopped | ✅ Done |
| `NotifyChannelFoundCallsStoreChannel` | Channel persisted | ✅ Done |
| `NotifyPairingDoneCallsOnPairingTimeout` | Pairing timeout handled | ✅ Done |
| `NotifyPairingDoneCheckForPeers` | Peer count checked | ✅ Done |
| `NotifyScanFailedCallsOnScanFailed` | Scan failure handled | ✅ Done |
| `NotifyScanFailedCallsStopScan` | Scan stopped | ✅ Done |
| `NotifyScanFailedCheckForPeers` | Peer count checked | ✅ Done |
| `NotifyChannelChangedChecksCurrentChannel` | Channel change detected | ✅ Done |
| `NotifyChannelChangedPropagatesChannel` | Channel updated in config | ✅ Done |
| `NotifyStopTurnsShouldStopTrue` | Task stop flag | ✅ Done |

### 1.9 Reconnect (NEW since original report)

| Test | Description | Status |
|-------|-----------|--------|
| `ReconnectReturnsInvalidStateWhenNotIdle` | State guard | ✅ Done |
| `ReconnectReturnsInvalidArgWhenNoPeers` | No peers guard | ✅ Done |
| `ReconnectWithPeersTransitionsToRecoveryScan` | IDLE → RECOVERY_SCAN | ✅ Done |
| `ReconnectResetsScanRetryCounter` | Retry counter reset | ✅ Done |

### 1.10 Build App Message

| Test | Description | Status |
|-------|-----------|--------|
| `BuildAppMessageWithDataPayloadCreatesAppMessage` | App message construction | ✅ Done |

### 1.11 Additional Host Tests (from BEHAVIORAL_TESTS_TODO.md)

| Test | Description | Status |
|-------|-----------|--------|
| `MaxRetriesExhaustedReportsDeliveryFailure` | ACK timeout after all retries → delivery failure | ✅ Done (test_tx_manager) |
| `QueueFullDropsExtraPackets` | App queue full → packets dropped gracefully | ✅ Done |
| `ReinitAfterDeinitSucceeds` | Full resource cleanup and re-init | ✅ Done |
| `InitWithNoPeersTransitionsToPairingScan` | Non-HUB without peers → PAIRING_SCAN | ✅ Done |
| `StartPairingWithoutPeersTransitionsToPairingScan` | Non-HUB pairing flow | ✅ Done |
| `HeartbeatTimeoutMarksPeerOffline` | Peer not sending heartbeats → marked offline | ⏳ Requires real hardware (see Section 3.4) |
| `ConcurrentSendAndReceive` | Thread safety under concurrent load | ⏳ Covered by task-based tests |
| `LruEvictionWhenMaxPeersReached` | 19 peers → LRU eviction | ⏳ Covered by PeerManager tests |
| `StorageSyncsRtcToNvs` | RTC → NVS sync on pairing | ⏳ Covered by StorageManager tests |

---

## 2. Host-Based Tests (with real tasks) - `test_espnow_manager_task.cpp`

**Status: 37 tests implemented ✅**

### 2.1 RX Task - Packet Processing

| Test | Description | Status |
|-------|-----------|--------|
| `ValidPacketCallsOnPacketReceived` | Valid packet → stats updated | ✅ Done |
| `InvalidCrcPacketIsNotRouted` | CRC invalid → dropped | ✅ Done |
| `InvalidCrcDoesNotCallOnPacketReceived` | CRC invalid → no stats | ✅ Done |
| `FailedHeaderDecodeIsNotRouted` | Header decode fail → dropped | ✅ Done |
| `FailedHeaderDecodeDoesNotCallOnPacketReceived` | Header fail → no stats | ✅ Done |
| `DataPacketIsNotRouted` | DATA → not routed | ✅ Done |
| `DataPacketIsDeliveredToAppQueue` | DATA → app queue | ✅ Done |
| `CommandPacketIsNotRouted` | COMMAND → not routed | ✅ Done |
| `CommandPacketIsDeliveredToAppQueue` | COMMAND → app queue | ✅ Done |
| `ProtocolPacketIsRoutedViaMessageRouter` | Protocol → router | ✅ Done |
| `ProtocolPacketDoesNotReachAppQueue` | Protocol → not to app | ✅ Done |
| `DataPacketRequiringAckDeliveredToAppQueueWithRequiresAckFlag` | ACK flag preserved | ✅ Done |
| `DataPacketWithRequiresAckDeliveredToAppQueueAndStoresHeader` | Header stored for ACK | ✅ Done |
| `DataPacketIncludesRssiInAppMessage` | RSSI included in app msg | ✅ Done |
| `ValidPacketNotifiesTxManagerLinkAlive` | Link alive notification | ✅ Done |

### 2.2 RX Task - Manager Notifications

| Test | Description | Status |
|-------|-----------|--------|
| `MaxFailuresFromOperationalTransitionsToRecoveryScan` | MAX_FAILURES → RECOVERY_SCAN | ✅ Done |
| `ChannelFoundFromPairingScanTransitionsToPairing` | CHANNEL_FOUND → PAIRING | ✅ Done |
| `ChannelFoundFromRecoveryScanTransitionsToOperational` | CHANNEL_FOUND → OPERATIONAL | ✅ Done |
| `ChannelFoundCallsPairingManagerStart` | Pairing started on channel found | ✅ Done |
| `ScanFailedTransitionsToIdle` | SCAN_FAILED → IDLE | ✅ Done |
| `PairingDoneWithNoPeersTransitionsToIdle` | PAIRING_DONE no peers → IDLE | ✅ Done |
| `PairingDoneWithPeersTransitionsToOperational` | PAIRING_DONE with peers → OPERATIONAL | ✅ Done |
| `ChannelChangedUpdatesConfigAndStorage` | CHANNEL_CHANGED → config + storage | ✅ Done |
| `NotifyStopDeletesRxTasAndClearHandle` | NOTIFY_STOP → task deleted | ✅ Done |

### 2.3 RX Task - Tick per State

| Test | Description | Status |
|-------|-----------|--------|
| `RxTaskCallsPairingTickWhenPairing` | PAIRING → pairing tick | ✅ Done |
| `RxTaskCallsChannelMonitorTickWhenOperational` | OPERATIONAL → channel tick | ✅ Done |
| `RxTaskCallsChannelMonitorTickWhenPairing` | PAIRING → channel tick | ✅ Done |
| `RxTaskCallsHeartbeatTickWhenOperational` | OPERATIONAL → heartbeat tick | ✅ Done |
| `RxTaskDoesNotCallsChannelMonitorTickWhenPairingScan` | PAIRING_SCAN → no channel tick | ✅ Done |
| `RxTaskDoesNotCallPairingTickWhenOperational` | OPERATIONAL → no pairing tick | ✅ Done |

### 2.4 Queue Behavior

| Test | Description | Status |
|-------|-----------|--------|
| `QueueFullDropsExtraPackets` | Queue full → packets dropped | ✅ Done |

### 2.5 Tick Scan Retry (NEW since original report)

| Test | Description | Status |
|-------|-----------|--------|
| `TickScanRetryInactiveDoesNothing` | Inactive → no-op | ✅ Done |
| `TickScanRetryBeforeNextAttemptDoesNothing` | Too early → no-op | ✅ Done |
| `TickScanRetryWhenNotIdleResetsAndDoesNothing` | Not IDLE → reset | ✅ Done |
| `TickScanRetryWhenIdleTriggersRecoveryScan` | IDLE + timeout → scan | ✅ Done |

### 2.6 Statistics (NEW since original report)

| Test | Description | Status |
|-------|-----------|--------|
| `GetPeerStatsReturnsDataAfterPacketReception` | Stats populated after RX | ✅ Done |
| `GetAllPeerStatsReturnsNonEmptyAfterReception` | All stats non-empty | ✅ Done |

---

## 3. On-Device Tests (real hardware required)

### 3.1 Multi-Device Communication

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationHubAndNodePairSuccessfully` | Real HUB + NODE → complete pairing | Critical | **OK** |
| `IntegrationNodeSendsDataHubReceivesAndAcks` | NODE → DATA → HUB → ACK | Critical | **OK** |
| `IntegrationAckTimeoutRetriesAndSuccess` | Timeout → retry → success | High | **OK** |
| `IntegrationMaxFailuresTriggersRecoveryScan` | Max failures → real scan | High | *Implicitly covered by HubChangesChannel* |

### 3.2 Channel Scanning

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationDiscoveryScanFindsHubOnDifferentChannel` | HUB on different channel → scan finds | Critical | **OK** |
| `IntegrationFullChannelScan1to13` | Full scan channels 1-13 | High | *not useful* |
| `IntegrationHubChangesChannelNodesRecover` | HUB changes channel → NODEs recover | Critical | **OK** |
| `IntegrationScanFailsWhenNoHubPresent` | Scan without HUB → expected failure | High | **OK** |

### 3.3 Pairing Protocol

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationPairingHubAcceptsNode` | HUB accepts NODE → peer added | Critical | *Covered by HubAndNodePairSuccessfully* |
| `IntegrationHubUpdatesNodeIdOnMacCollision` | HUB updates peer ID when same MAC re-pairs with different ID | Critical | **OK** |
| `IntegrationPairingTimeoutWithoutResponse` | Pairing timeout without response | High | *Implicitly covered by ScanFailsWhenNoHubPresent* |

### 3.4 Heartbeat & Link Monitoring

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationHeartbeatSentPeriodically` | Heartbeats sent periodically | High | **OK** |
| `IntegrationHeartbeatTimeoutMarksPeerOffline` | Heartbeat timeout → peer offline | High | **OK** |
| `IntegrationHeartbeatResetsOfflineTimer` | Heartbeat received → reset timer | High | **OK** |
| `IntegrationGetOfflinePeersReturnsCorrectList` | Correct offline peers list | High | *Covered by HeartbeatTimeoutMarksPeerOffline* |

### 3.5 Persistent Storage

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationPeersPersistedToNvsAndRestored` | Peers persisted to NVS → restored | Critical | **OK** |
| `IntegrationChannelPersistedAndRestored` | Channel persisted → restored | High | *Covered by PeersPersistedToNvsAndRestored* |
| `IntegrationNodeRebootsAndReconnects` | Node reboots (init/deinit) and reconnects without re-pairing | Critical | *Implicitly covered by HeartbeatResetsOfflineTimer* |
| `IntegrationHubFullPeerListEviction` | Hub list overflow (LRU) evicts oldest peer | High | |
| `IntegrationRtcStorageSurvivesDeepSleep` | RTC RAM survives deep sleep | High | **OK** |
| `IntegrationSyncRtcToNvsOnPairingSuccess` | Sync RTC → NVS on pairing | High | *Covered by NvsBackupUsedWhenRtcCorrupt* |

### 3.6 Deep Sleep & Wake-up

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationNodeWakesFromDeepSleepWithPeersIntact` | Wake-up with peers intact | Critical | **OK** |
| `IntegrationNodeOperationalImmediatelyAfterWake` | Operational immediately after wake | High | *Covered by NodeWakesFromDeepSleep* |
| `IntegrationNoRePairingRequiredAfterWake` | No re-pairing after wake | Critical | *Covered by NodeWakesFromDeepSleep* |
| `IntegrationNvsBackupUsedWhenRtcCorrupt` | NVS used when RTC corrupted | Medium | **OK** |

### 3.7 Stress & Performance

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationDataStressTest` | High frequency transmission (100+ packets) | Medium | **OK** |

### 3.8 Edge Cases & Error Handling

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `EdgeCaseHubGoesOfflineNodesRecover` | HUB offline → NODEs recover | High | *Implicitly covered by HeartbeatTimeoutMarksPeerOffline* |
| `EdgeCasePeerRemovedDuringTransmission` | Peer removed during transmission | Medium | |
| `EdgeCaseMalformedPacketsIgnored` | Malformed packets → ignored | High | **OK** |
| `EdgeCaseDuplicateSequenceNumbersHandled` | Duplicate sequence numbers | Medium | |
| `EdgeCaseRssiBasedLinkQualityMonitoring` | RSSI for link quality | Low | |

---

## Priority Summary

### Host-Based Tests: ✅ All Implemented
- **Section 1** (`test_espnow_manager.cpp`): **87 tests** — all implemented + reconnect, statistics
- **Section 2** (`test_espnow_manager_task.cpp`): **37 tests** — all implemented + scan retry, statistics

### On-Device Tests (Section 3): Partially Verified on Hardware
- **Verified OK**: 16 tests confirmed on real hardware
- **Covered by other tests**: 8 tests implicitly covered
- **Not yet tested**: 4 edge cases (peer removal, duplicate seq, RSSI monitoring)
- **Not useful**: 1 test (full channel scan 1-13)
- **See `test_apps/`** for on-device integration test scaffolding

---

## Totals

| Category | Planned | Implemented | Status |
|-----------|---------|-------------|--------|
| Host-based (no tasks) | ~50 | **87** | ✅ Complete + expanded |
| Host-based (with tasks) | ~35 | **37** | ✅ Complete |
| On-device | ~35 | 0 | ⏳ Hardware required |
| **Total** | **~120** | **124** | **124/124 host tests pass** |

---

## Notes

1. **Host-Based Tests**: Executed on Linux using Google Test + Google Mock with mocked HALs. All 124 tests pass via `ctest`.
2. **On-Device Tests**: Require real ESP32 hardware and at least 2 devices (1 HUB + 1 NODE). See `test_apps/` for scaffolding.
3. **Current Coverage**: All planned host-based tests from the original report are implemented. New tests were added for:
   - `reconnect()` API and exponential backoff scan retry
   - `get_peer_stats()` / `get_all_peer_stats()` public statistics API
   - `tick_scan_retry()` exponential backoff logic
4. **Coverage Report**: Available at `host_test/coverage/index.html` via `make run_all_tests`.

---

*Document updated: 2026-04-12*


--- End of content ---

## 4. Recent Changes (2026-04-01)

### 4.1 Exponential Backoff for RECOVERY_SCAN
Implemented exponential backoff logic for recovery scans to prevent infinite looping when a HUB is lost and not found again.

- **Maximum Retries**: 7 attempts (configurable via `EspNowConfig.scan_max_retries`).
- **Base Backoff**: 2000ms, doubling each attempt (2s, 4s, 8s, 16s, 32s, 64s, 128s).
- **Total Recovery Window**: ~4 minutes and 14 seconds before transitioning to `IDLE` permanently.
- **Manual Overriding**: Added `reconnect()` public method to restart the recovery process from `IDLE`.
- **Isolation**: Pairing processes (`PAIRING_SCAN`) remain unaffected, preserving their own internal timeout and retry logic.

### 4.2 API Additions
- `IEspNowManager::reconnect()`: Resets the scan retry counter and triggers a `RECOVERY_SCAN`.
- `EspNowConfig::scan_max_retries`: Allows disabling or modifying the retry behavior.

### TODO: Next Testing Steps
- [ ] **Test Review**: Update and run integration tests (especially `IntegrationScanFailsWhenNoHubPresent`) to verify they handle the new `IDLE` state transition correctly.
- [ ] **Unit Tests**: Implement host-side tests for `tick_scan_retry` to verify the mathematical doubling of delays and the max retry limit.
- [ ] **Reconnect Strategy**: Verify that calling `reconnect()` correctly transitions the node back to `RECOVERY_SCAN` and resets the backoff state.

