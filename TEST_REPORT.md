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

### 1.1 Initialization & Configuration

| Test | Description | Priority |
|-------|-----------|------------|
| `InitLoadsChannelFromStorageAndUsesIt` | Verify that channel loaded from storage is used on initialization | High |
| `InitWithStoredChannelOverridesConfigChannel` | Channel from storage must override config channel | High |
| `InitFailsWhenStorageLoadChannelFails` | Behavior when load_channel fails | Medium |
| `InitCallsLoadPeersFromStorage` | Verify that peers are loaded from storage | High |
| `InitWithEmptyPeerListTransitionsToPairingScan` | No peers → PAIRING_SCAN | High |
| `InitWithPeersAddsPeersToEspNow` | With peers → add to ESP-NOW and go to OPERATIONAL | High |
| `InitSetsScannerChannelFromConfig` | Scanner must receive correct channel | Medium |
| `InitStoresChannelAtEndOfInitialization` | Channel must be persisted at end of init | Medium |

### 1.2 State Transitions on Initialization

| Test | Description | Priority |
|-------|-----------|------------|
| `InitHubWithPeersTransitionsToOperational` | HUB with peers → OPERATIONAL | High |
| `InitHubWithoutPeersTransitionsToPairing` | HUB without peers → PAIRING (not PAIRING_SCAN) | High |
| `InitNodeWithPeersTransitionsToOperational` | NODE with peers → OPERATIONAL | High |
| `InitNodeWithoutPeersTransitionsToPairingScan` | NODE without peers → PAIRING_SCAN | High |

### 1.3 Deinitialization

| Test | Description | Priority |
|-------|-----------|------------|
| `DeinitCallsAllManagerDeinitMethods` | Verify that all managers receive deinit() | High |
| `DeinitDeletesAllFreeRTOSResources` | Verify deletion of mutex, queues, tasks | High |
| `DeinitRemovesAllPeersFromEspNow` | Delete all peers from ESP-NOW | High |
| `DeinitTransitionsNodeStateToUninitialized` | Final state must be UNINITIALIZED | High |
| `DeinitCanBeCalledMultipleTimesSafely` | deinit() idempotency | Medium |
| `ReinitAfterDeinitSucceeds` | Re-initialization after deinit must work | High |

### 1.4 Pairing

| Test | Description | Priority |
|-------|-----------|------------|
| `StartPairingStoresTimeoutAndTransitionsState` | start_pairing() stores timeout and transitions state | High |
| `StartPairingInUninitializedReturnsInvalidState` | Guard against invalid state | High |
| `StartPairingHubWithPeersTransitionsToPairing` | HUB with peers → PAIRING | High |
| `StartPairingNodeWithoutPeersTransitionsToPairingScan` | NODE without peers → PAIRING_SCAN | High |

### 1.5 Data Transmission

| Test | Description | Priority |
|-------|-----------|------------|
| `SendDataInNonOperationalStateReturnsInvalidState` | State guard | High |
| `SendDataToUnknownPeerReturnsNotFound` | Peer not found → ESP_ERR_NOT_FOUND | High |
| `SendDataWithValidPayloadQueuesPacket` | Valid payload → queue_packet called | High |
| `SendDataWithNullPayloadAndZeroLenSucceeds` | Header-only packet must work | Medium |
| `SendDataWithOversizedPayloadReturnsInvalidArg` | Payload > MAX_PAYLOAD_SIZE → error | High |
| `SendCommandFollowsSamePathAsSendData` | send_command() uses same path | Medium |

### 1.6 Reception Confirmation (ACK)

| Test | Description | Priority |
|-------|-----------|------------|
| `ConfirmReceptionInNonOperationalReturnsInvalidState` | State guard | High |
| `ConfirmReceptionWithoutStoredHeaderReturnsInvalidState` | No header stored → error | High |
| `ConfirmReceptionWithStoredHeaderSendsAckPacket` | Header stored → sends ACK | High |
| `ConfirmReceptionClearsStoredHeaderAfterSend` | Header is cleared after send | High |
| `ConfirmReceptionWithUnknownPeerReturnsNotFound` | Peer not found → error | High |
| `ConfirmReceptionWithSemaphoreTimeoutReturnsTimeout` | Timeout on semaphore → ESP_ERR_TIMEOUT | Medium |

### 1.7 Peer Management

| Test | Description | Priority |
|-------|-----------|------------|
| `AddPeerDelegatesToPeerManager` | add_peer() delegates to peer_mgr_ | High |
| `RemovePeerDelegatesToPeerManager` | remove_peer() delegates to peer_mgr_ | High |
| `GetOfflinePeersReturnsEmptyInNonOperational` | Non-operational state → empty list | High |
| `GetOfflinePeersDelegatesToPeerManager` | get_offline_peers() delegates to peer_mgr_ | High |
| `GetPeersReturnsAllPeersFromPeerManager` | get_peers() returns all peers | High |

### 1.8 Notifications & State Transitions

| Test | Description | Priority |
|-------|-----------|------------|
| `HandleNotificationsMaxFailuresTriggersScan` | NOTIFY_MAX_FAILURES → on_scan_requested() | High |
| `HandleNotificationsChannelFoundUpdatesConfig` | NOTIFY_CHANNEL_FOUND → update config_.wifi_channel | High |
| `HandleNotificationsChannelFoundTransitionsState` | NOTIFY_CHANNEL_FOUND → on_channel_found() | High |
| `HandleNotificationsPairingDoneWithPeersToOperational` | NOTIFY_PAIRING_DONE + peers → OPERATIONAL | High |
| `HandleNotificationsPairingDoneWithoutPeersToIdle` | NOTIFY_PAIRING_DONE without peers → IDLE | High |
| `HandleNotificationsScanFailedTransitionsToIdle` | NOTIFY_SCAN_FAILED → IDLE | High |
| `HandleNotificationsChannelChangedUpdatesConfigAndStorage` | NOTIFY_CHANNEL_CHANGED → update + store | High |
| `HandleNotificationsStopSetsShouldStopFlag` | NOTIFY_STOP → should_stop = true | High |

### 1.9 Handle State Transition

| Test | Description | Priority |
|-------|-----------|------------|
| `HandleStateTransitionNoOpWhenStateUnchanged` | Same state → no action | High |
| `HandleStateTransitionToPairingScanStartsScanner` | → PAIRING_SCAN → scanner_->start_scan() | High |
| `HandleStateTransitionToRecoveryScanStartsScanner` | → RECOVERY_SCAN → scanner_->start_scan() | High |
| `HandleStateTransitionToPairingStopsScannerAndStartsPairing` | → PAIRING → stop_scan + start() | High |
| `HandleStateTransitionToOperationalStopsScannerAndStoresChannel` | → OPERATIONAL → stop_scan + store_channel | High |
| `HandleStateTransitionToIdleStopsScanner` | → IDLE → stop_scan() | High |
| `HandleStateTransitionFromRecoveryScanStoresChannel` | From RECOVERY_SCAN → OPERATIONAL stores channel | High |

### 1.10 Build App Message

| Test | Description | Priority |
|-------|-----------|------------|
| `BuildAppMessageExtractsCorrectFields` | Extract correct fields from decoded | High |
| `BuildAppMessageCopiesSrcMac` | Source MAC is copied | High |
| `BuildAppMessageCalculatesCorrectPayloadLength` | payload_len = len - header - CRC | High |
| `BuildAppMessageCopiesPayload` | Payload is copied correctly | High |

---

## 2. Host-Based Tests (with real tasks) - `test_espnow_manager_task.cpp`

### 2.1 RX Task - Packet Processing

| Test | Description | Priority |
|-------|-----------|------------|
| `RxTaskValidCrcAndHeaderCallsMessageRouter` | CRC + header valid → router called | High |
| `RxTaskInvalidCrcDropsPacket` | CRC invalid → packet dropped | High |
| `RxTaskInvalidHeaderDropsPacket` | Header invalid → packet dropped | High |
| `RxTaskDataPacketDeliversToAppQueue` | DATA → app_rx_queue | High |
| `RxTaskCommandPacketDeliversToAppQueue` | COMMAND → app_rx_queue | High |
| `RxTaskProtocolPacketCallsMessageRouter` | Protocol packets → router | High |
| `RxTaskDataPacketRequiringAckStoresHeader` | requires_ack=true → stores header | High |
| `RxTaskDataPacketNotRequiringAckDoesNotStoreHeader` | requires_ack=false → does not store | High |
| `RxTaskNotifiesTxManagerLinkAliveOnValidPacket` | Valid packet → notify_link_alive() | High |

### 2.2 RX Task - Manager Notifications

| Test | Description | Priority |
|-------|-----------|------------|
| `RxTaskNotifyMaxFailuresTriggersRecoveryScan` | NOTIFY_MAX_FAILURES → RECOVERY_SCAN | High |
| `RxTaskNotifyMaxFailuresWithoutPeersTriggersPairingScan` | NOTIFY_MAX_FAILURES without peers → PAIRING_SCAN | High |
| `RxTaskNotifyChannelFoundFromPairingScanToPairing` | CHANNEL_FOUND from PAIRING_SCAN → PAIRING | High |
| `RxTaskNotifyChannelFoundFromRecoveryScanToOperational` | CHANNEL_FOUND from RECOVERY_SCAN → OPERATIONAL | High |
| `RxTaskNotifyScanFailedTransitionsToIdle` | NOTIFY_SCAN_FAILED → IDLE | High |
| `RxTaskNotifyPairingDoneWithPeersToOperational` | PAIRING_DONE + peers → OPERATIONAL | High |
| `RxTaskNotifyPairingDoneWithoutPeersToIdle` | PAIRING_DONE sem peers → IDLE | High |
| `RxTaskNotifyChannelChangedUpdatesConfigAndStorage` | CHANNEL_CHANGED → update config + storage | High |
| `RxTaskNotifyStopTerminatesTask` | NOTIFY_STOP → task terminates | High |

### 2.3 RX Task - Tick per State

| Test | Description | Priority |
|-------|-----------|------------|
| `RxTaskTicksPairingManagerWhenPairing` | State PAIRING → pairing_mgr_->tick() | High |
| `RxTaskTicksChannelMonitorWhenPairing` | State PAIRING → channel_monitor_->tick() | High |
| `RxTaskTicksChannelMonitorWhenOperational` | State OPERATIONAL → channel_monitor_->tick() | High |
| `RxTaskTicksHeartbeatManagerWhenOperational` | State OPERATIONAL → heartbeat_mgr_->tick() | High |
| `RxTaskDoesNotTickManagersWhenPairingScan` | State PAIRING_SCAN → do not tick managers | High |
| `RxTaskDoesNotTickManagersWhenRecoveryScan` | State RECOVERY_SCAN → do not tick managers | High |
| `RxTaskDoesNotTickManagersWhenIdle` | State IDLE → do not tick managers | High |

### 2.4 Behavior Scenarios

| Test | Description | Priority |
|-------|-----------|------------|
| `ScenarioNodeBootsWithoutPeersAndScansForHub` | NODE boot without peers → PAIRING_SCAN | High |
| `ScenarioNodeFindsHubAndTransitionsToOperational` | NODE finds HUB → OPERATIONAL | High |
| `ScenarioNodePairsSuccessfullyWithHub` | Pairing complete → peer added | High |
| `ScenarioNodeSendsDataToHub` | NODE sends DATA → HUB receives | High |
| `ScenarioHubReceivesDataAndSendsAck` | HUB receives DATA → sends ACK | High |
| `ScenarioHubChangesChannelAndNodeRecovers` | HUB changes channel → NODE does recovery scan | High |
| `ScenarioNodeLosesConnectionAndEntersRecoveryScan` | Connection lost → RECOVERY_SCAN | High |
| `ScenarioMultipleTransmissionFailuresTriggerScan` | MAX_FAILURES → scan | High |
| `ScenarioPairingTimeoutWithSuccess` | Pairing timeout with success → OPERATIONAL | High |
| `ScenarioPairingTimeoutWithoutSuccess` | Pairing timeout without success → IDLE | High |

### 2.5 Queue Behavior

| Test | Description | Priority |
|-------|-----------|------------|
| `RxTaskQueueFullDropsExtraPackets` | Queue full → packets dropped | High |
| `RxTaskProcessesQueueAfterSpaceAvailable` | Queue with space → processes packets | High |
| `RxTaskHandlesBurstOfPackets` | Burst of packets → all processed | Medium |

---

## 3. On-Device Tests (real hardware required)

### 3.1 Multi-Device Communication

| Test | Description | Priority |
|-------|-----------|------------|
| `IntegrationHubAndNodePairSuccessfully` | Real HUB + NODE → complete pairing | Critical |
| `IntegrationNodeSendsDataHubReceivesAndAcks` | NODE → DATA → HUB → ACK | Critical |
| `IntegrationHubReceivesFromMultipleNodes` | HUB receives from multiple NODEs | Critical |
| `IntegrationMultipleNodesSimultaneousTransmission` | Multiple NODEs transmit simultaneously | High |
| `IntegrationAckTimeoutRetriesAndSuccess` | Timeout → retry → success | High |
| `IntegrationMaxFailuresTriggersRecoveryScan` | Max failures → real scan | High |

### 3.2 Channel Scanning

| Test | Description | Priority |
|-------|-----------|------------|
| `IntegrationDiscoveryScanFindsHubOnDifferentChannel` | HUB on different channel → scan finds | Critical |
| `IntegrationFullChannelScan1to13` | Full scan channels 1-13 | High |
| `IntegrationHubChangesChannelNodesRecover` | HUB changes channel → NODEs recover | Critical |
| `IntegrationScanFailsWhenNoHubPresent` | Scan without HUB → expected failure | High |

### 3.3 Pairing Protocol

| Test | Description | Priority |
|-------|-----------|------------|
| `IntegrationPairingHubAcceptsNode` | HUB accepts NODE → peer added | Critical |
| `IntegrationPairingHubRejectsAnotherHub` | HUB rejects another HUB | High |
| `IntegrationPairingTimeoutWithoutResponse` | Pairing timeout without response | High |
| `IntegrationPairingMultipleNodesSequentially` | Multiple NODEs pair sequentially | High |
| `IntegrationPairingMaxPeersReached` | 19 peers → reject new | Medium |

### 3.4 Heartbeat & Link Monitoring

| Test | Description | Priority |
|-------|-----------|------------|
| `IntegrationHeartbeatSentPeriodically` | Heartbeats sent periodically | High |
| `IntegrationHeartbeatTimeoutMarksPeerOffline` | Heartbeat timeout → peer offline | High |
| `IntegrationHeartbeatResetsOfflineTimer` | Heartbeat received → reset timer | High |
| `IntegrationGetOfflinePeersReturnsCorrectList` | Correct offline peers list | High |

### 3.5 Persistent Storage

| Test | Description | Priority |
|-------|-----------|------------|
| `IntegrationPeersPersistedToNvsAndRestored` | Peers persisted to NVS → restored | Critical |
| `IntegrationChannelPersistedAndRestored` | Channel persisted → restored | High |
| `IntegrationRtcStorageSurvivesDeepSleep` | RTC RAM survives deep sleep | High |
| `IntegrationNvsBackupUsedWhenRtcCorrupt` | NVS used when RTC corrupted | Medium |
| `IntegrationSyncRtcToNvsOnPairingSuccess` | Sync RTC → NVS on pairing | High |

### 3.6 Deep Sleep & Wake-up

| Test | Description | Priority |
|-------|-----------|------------|
| `IntegrationNodeWakesFromDeepSleepWithPeersIntact` | Wake-up with peers intact | Critical |
| `IntegrationNodeOperationalImmediatelyAfterWake` | Operational immediately after wake | High |
| `IntegrationNoRePairingRequiredAfterWake` | No re-pairing after wake | Critical |

### 3.7 Stress & Performance

| Test | Description | Priority |
|-------|-----------|------------|
| `StressHighFrequencyDataTransmission` | High frequency transmission | Medium |
| `StressLongRunningOperationHours` | Continuous operation (hours) | High |
| `StressRapidPairingUnpairingCycles` | Rapid pairing/unpairing cycles | Medium |
| `StressChannelInterferenceEnvironment` | Environment with interference | High |
| `StressMaximumPeersConcurrentCommunication` | 19 peers communicating | High |

### 3.8 Edge Cases & Error Handling

| Test | Description | Priority |
|-------|-----------|------------|
| `EdgeCaseHubGoesOfflineNodesRecover` | HUB offline → NODEs recover | High |
| `EdgeCasePeerRemovedDuringTransmission` | Peer removed during transmission | Medium |
| `EdgeCaseMalformedPacketsIgnored` | Malformed packets → ignored | High |
| `EdgeCaseDuplicateSequenceNumbersHandled` | Duplicate sequence numbers | Medium |
| `EdgeCaseRssiBasedLinkQualityMonitoring` | RSSI for link quality | Low |

---

## Priority Summary

### Critical Priority (implement first)
- Initialization with/without peers
- Basic state transitions
- Basic HUB-NODE communication
- Basic pairing
- Peer persistence

### High Priority
- All manager notifications
- Handle state transitions
- RX task processing
- Recovery scan
- Heartbeat monitoring

### Medium Priority
- Edge cases
- Error handling detailed
- Stress tests
- Performance tests

### Low Priority
- RSSI monitoring
- Performance optimizations

---

## Totals

| Category | Quantity |
|-----------|------------|
| Host-based (no tasks) | ~50 testes |
| Host-based (with tasks) | ~35 testes |
| On-device | ~35 testes |
| **Total** | **~120 testes** |

---

## Notes

1. **Host-Based Tests**: Can be executed on Linux using Google Test + Google Mock with mocked HALs.
2. **On-Device Tests**: Require real ESP32 hardware and at least 2 devices (1 HUB + 1 NODE).
3. **Current Coverage**: Existing tests partially cover categories 1.1, 1.2, 1.3, 1.5, 1.6, 2.1, 2.2, and 2.3.
4. **Main Gaps**: On-device integration tests, stress tests, and complete behavior scenarios.

---

*Document generated: 2026-03-31*

---

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

