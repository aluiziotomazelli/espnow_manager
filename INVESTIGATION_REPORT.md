# Investigation Report: NodeState Loop in Multi-Device Tests

## 1. Problem Description

When running on-target multi-device tests, the NODE enters an infinite loop alternating between `NodeState::SCANNING` and `NodeState::OPERATIONAL`. The pairing process never completes despite the HUB being found on the correct channel.

Logs show:
```
I (10153) DiscoveryMgr: Hub found on channel 1.
I (10153) DiscoveryMgr: Starting channel scan to find Hub.  ← immediate restart
...
I (10173) EspNowManager: NodeState: 3 -> 2  (SCANNING -> OPERATIONAL)
I (10173) EspNowManager: NodeState: 2 -> 3  (OPERATIONAL -> SCANNING)
```

## 2. Root Cause Analysis

After a deep-dive into the codebase, four critical issues were identified:

### 2.1. FSM Stuck in SCANNING (Infinite Scan Loop)
The `TxStateMachine::on_link_alive()` method resets the failure counter but fails to transition the state from `SCANNING` back to `IDLE`.
- **Location:** `src/tx_state_machine.cpp`
- **Impact:** `TxManager` task remains in the `TxState::SCANNING` case and immediately restarts the scan.

### 2.2. Inverted HUB/NODE Logic in Pairing Start
In `EspNowManager::handle_notifications()`, the condition to start the `PairingManager` after a successful channel scan is inverted.
- **Location:** `src/espnow_manager.cpp:514`
- **Impact:** HUBs (which don't scan) are told to start pairing upon a scan notification, while NODEs (which do scan) skip starting the pairing handshake.

### 2.3. Premature Transition to OPERATIONAL
The `NOTIFY_CHANNEL_FOUND` handler unconditionally transitions the node to `NodeState::OPERATIONAL`.
- **Location:** `src/espnow_manager.cpp:517`
- **Impact:** For a NODE, this transition happens before the pairing handshake (PAIR_REQUEST/PAIR_RESPONSE) is complete.

### 2.4. Missing Pairing Completion Detection
`EspNowManager` does not observe the `PairingManager` to detect when a handshake is successful.
- **Impact:** A NODE remains in its internal logic stuck between states because it never officially transitions to `OPERATIONAL` based on pairing success.

## 3. Proposed Solutions

### 3.1. Fix TxStateMachine Transitions
- Update `ITxStateMachine::on_link_alive()` to return the next `TxState`.
- Implement the transition: if `current_state_ == TxState::SCANNING`, move to `TxState::IDLE`.

### 3.2. Correct EspNowManager Notification Handling
- Fix the inverted logic: start `pairing_manager_` only if `node_type != HUB`.
- Remove the premature `transition_to_state(NodeState::OPERATIONAL)` in the `NOTIFY_CHANNEL_FOUND` handler for non-HUB nodes.

### 3.3. Implement Pairing Completion Observer
- In the `rx_task` loop, check if the node is in `NodeState::PAIRING` and if `pairing_manager_->is_active()` has become `false`.
- Transition to `NodeState::OPERATIONAL` only after successful pairing (peer list not empty).

### 3.4. Improve Protocol Routing
**Update**: The `CHANNEL_SCAN_RESPONSE` is already handled perfectly within `EspNowManager::rx_task` directly notifying `TxManager` before any routing. No `MessageRouter` changes are required to avoid duplicate notifications.

## 4. Verification Plan
1. Apply the fixes to `src/` and `include/`.
2. Run host-based unit tests (`host_test/`) to ensure no regressions in individual managers.
3. Run the multi-device test `test_multiple_devices` on physical hardware to confirm the NODE completes pairing and stays in `OPERATIONAL` state.
