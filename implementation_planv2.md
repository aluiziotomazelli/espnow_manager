# Implementation Plan: Synchronize Node and Tx State Machines

The goal is to eliminate the redundant `TxState::SCANNING` state and synchronize channel discovery with the global `NodeStateMachine`.

## Background: Root Cause

`TxManager` owns its own `TxState::SCANNING` state and drives the `DiscoveryManager::scan()` call **synchronously, inside its own task**. When `NodeStateMachine` was added, a second independent `NodeState::SCANNING` state was layered on top. The two are independent and can diverge:

- `NodeState::SCANNING` is transitioned by `EspNowManager` via `NOTIFY_SCANNING`.
- `TxState::SCANNING` is entered by `TxManager` autonomously on `MAX_FAILURES`.

The fix is to move *ownership of the scan decision* up to `EspNowManager` (where `NodeStateMachine` lives), and make `DiscoveryManager` a **passive, non-blocking service** driven by ticks from the `rx_task`.

## User Review Required

> [!IMPORTANT]
> `TxState::SCANNING` will be removed. `TxManager` will no longer automatically perform scans when transmission fails. Instead, it will notify `EspNowManager`, which will decide whether to enter `NodeState::SCANNING` and start a discovery process.

## Proposed Changes

### [Interfaces]

#### [MODIFY] [i_channel_observer.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/interfaces/i_channel_observer.hpp)
- Add `on_max_failures_cb()` to notify when a node is likely unreachable.

#### [MODIFY] [i_tx_manager.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/interfaces/i_tx_manager.hpp)
- Add `set_observer(IChannelObserver* observer)` to allow `TxManager` to report failures.

### [Core Logic]

#### [MODIFY] [tx_state_machine.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/tx_state_machine.hpp) and [.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/tx_state_machine.cpp)
- Remove `TxState::SCANNING`.
- Remove `on_scan_requested`.
- Update `on_physical_fail` to transition to `IDLE` after `MAX_FAILURES`.

#### [MODIFY] [tx_manager.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/tx_manager.hpp) and [.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/tx_manager.cpp)
- Remove `TxState::SCANNING` case from `tx_task`.
- Implement `set_observer`.
- In `tx_task`, call `observer_->on_max_failures_cb()` when FSM reaches max failures.

#### [MODIFY] [i_discovery_manager.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/interfaces/i_discovery_manager.hpp)

Add task lifecycle methods:

```cpp
// Initialize the discovery task (stack size 4KB-6KB)
virtual esp_err_t init(NodeId id, NodeType type, IChannelObserver *observer = nullptr) = 0;

// Notify the internal task to start a scan. Non-blocking.
virtual esp_err_t start_scan() = 0;

// Called from rx_task when a CHANNEL_SCAN_RESPONSE is received.
// Unblocks the internal scan loop.
virtual void handle_scan_response(const DecodedPacket &decoded) = 0;
```

---

#### [MODIFY] [discovery_manager.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/discovery_manager.hpp)

Add task handle and synchronization:

```cpp
private:
    TaskHandle_t task_handle_ = nullptr;
    static void discovery_task_func(void *arg);
    void discovery_task();

    // Synchronization between rx_task and discovery_task
    volatile bool response_received_ = false;
```

---

#### [MODIFY] [discovery_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/discovery_manager.cpp)

Implement the dedicated task:

1.  **`start_scan()`**: Simple `xTaskNotifyGive(task_handle_)`.
2.  **`discovery_task()`**:
    ```cpp
    while(1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY); // Wait for start signal
        observer_->on_scan_started_cb();
        performing_sequential_scan_loop(); // Same logic as current scan()
    }
    ```
3.  **`handle_scan_response()`**: Sets `response_received_ = true` and notifies the task if it's waiting on a specific channel.

---

#### [MODIFY] [espnow_manager.cpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/src/espnow_manager.cpp)

- `init()`: `DiscoveryManager` now creates its own task (alleviating `rx_task`).
- `on_max_failures_cb()`: Call `node_fsm_->on_scan_requested()` then `scanner_->start_scan()`.
- `rx_task()`: No longer needs to `tick()` the scanner, it just routes response packets.

### [Config Updates]

#### [MODIFY] [espnow_types.hpp](file:///home/german/dev/workspaces/idf-components/espnow_manager/include/espnow_types.hpp)
- Increase default stack sizes for `rx_task` and `tx_task` to `6144` (6KB) based on target testing feedback.
- Add `stack_size_discovery_task` to `EspNowConfig` (default `4096`).


## Verification Plan

### Automated Tests
- Run `host_test/test_tx_state_machine` to verify FSM transitions without `SCANNING`.
- Run `host_test/test_tx_manager` to verify failure notifications.
- Run `host_test/test_discovery_manager` (after updating it for async scan).
- Run `host_test/test_espnow_manager` to verify full integration.

Commands:
```bash
cd host_test/test_tx_state_machine && idf.py set-target linux && idf.py build && ./build/test_tx_state_machine.elf
cd host_test/test_tx_manager && idf.py set-target linux && idf.py build && ./build/test_tx_manager.elf
cd host_test/test_discovery_manager && idf.py set-target linux && idf.py build && ./build/test_discovery_manager.elf
cd host_test/test_espnow_manager && idf.py set-target linux && idf.py build && ./build/test_espnow_manager.elf
```

### Manual Verification
- Deploy to two ESP32 devices.
- Turn off the Hub.
- Observe the Node transitioning to `SCANNING` after failed transmissions.
- Turn on the Hub on a different channel.
- Observe the Node regaining connection.
