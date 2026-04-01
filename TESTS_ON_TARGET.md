## 3. On-Device Tests (real hardware required)

### 3.1 Multi-Device Communication

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationHubAndNodePairSuccessfully` | Real HUB + NODE → complete pairing | Critical | **OK** |
| `IntegrationNodeSendsDataHubReceivesAndAcks` | NODE → DATA → HUB → ACK | Critical | **OK** |
| `IntegrationAckTimeoutRetriesAndSuccess` | Timeout → retry → success | High | **OK** |
| `IntegrationMaxFailuresTriggersRecoveryScan` | Max failures → real scan | High | |

### 3.2 Channel Scanning

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationDiscoveryScanFindsHubOnDifferentChannel` | HUB on different channel → scan finds | Critical | **OK** |
| `IntegrationFullChannelScan1to13` | Full scan channels 1-13 | High | |
| `IntegrationHubChangesChannelNodesRecover` | HUB changes channel → NODEs recover | Critical | **OK** |
| `IntegrationScanFailsWhenNoHubPresent` | Scan without HUB → expected failure | High | **OK** |

### 3.3 Pairing Protocol

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationPairingHubAcceptsNode` | HUB accepts NODE → peer added | Critical | |
| `IntegrationPairingTimeoutWithoutResponse` | Pairing timeout without response | High | |


### 3.4 Heartbeat & Link Monitoring

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationHeartbeatSentPeriodically` | Heartbeats sent periodically | High | **OK** |
| `IntegrationHeartbeatTimeoutMarksPeerOffline` | Heartbeat timeout → peer offline | High | **OK** |
| `IntegrationHeartbeatResetsOfflineTimer` | Heartbeat received → reset timer | High | |
| `IntegrationGetOfflinePeersReturnsCorrectList` | Correct offline peers list | High | |

### 3.5 Persistent Storage

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationPeersPersistedToNvsAndRestored` | Peers persisted to NVS → restored | Critical | **OK** |
| `IntegrationChannelPersistedAndRestored` | Channel persisted → restored | High | |
| `IntegrationRtcStorageSurvivesDeepSleep` | RTC RAM survives deep sleep | High | |
| `IntegrationNvsBackupUsedWhenRtcCorrupt` | NVS used when RTC corrupted | Medium | |
| `IntegrationSyncRtcToNvsOnPairingSuccess` | Sync RTC → NVS on pairing | High | |

### 3.6 Deep Sleep & Wake-up

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `IntegrationNodeWakesFromDeepSleepWithPeersIntact` | Wake-up with peers intact | Critical | |
| `IntegrationNodeOperationalImmediatelyAfterWake` | Operational immediately after wake | High | |
| `IntegrationNoRePairingRequiredAfterWake` | No re-pairing after wake | Critical | |

### 3.7 Stress & Performance

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `StressHighFrequencyDataTransmission` | High frequency transmission | Medium | |


### 3.8 Edge Cases & Error Handling

| Test | Description | Priority | Status |
|-------|-----------|------------|--------|
| `EdgeCaseHubGoesOfflineNodesRecover` | HUB offline → NODEs recover | High | |
| `EdgeCasePeerRemovedDuringTransmission` | Peer removed during transmission | Medium | |
| `EdgeCaseMalformedPacketsIgnored` | Malformed packets → ignored | High | |
| `EdgeCaseDuplicateSequenceNumbersHandled` | Duplicate sequence numbers | Medium | |
| `EdgeCaseRssiBasedLinkQualityMonitoring` | RSSI for link quality | Low | |

---