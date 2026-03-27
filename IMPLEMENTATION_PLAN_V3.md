# Implementation Plan V3: Synchronize Node and Tx State Machines

**Status:** Ready for Implementation  
**Review Date:** 2026-03-25  
**Target:** ESP-IDF v5.1.1+  
**Language:** C++17

---

## Executive Summary

This plan fixes the **duplicated SCANNING responsibility** between `TxStateMachine` and `NodeStateMachine` by:

1. Removing `TxState::SCANNING` (TxManager only tracks packet transmission)
2. Creating a thread-safe observer pattern for failure notifications
3. Making `DiscoveryManager` asynchronous with proper synchronization
4. Adding comprehensive error handling and null safety

**Key Improvement over V2:** Thread safety is now a first-class concern with atomic operations, mutex protection, and proper FreeRTOS primitives.

---

## 1. Architecture Overview

### 1.1 State Machine Responsibilities

| Component | Owns | Does NOT Own |
|-----------|------|--------------|
| **TxStateMachine** | Packet transmission states: `IDLE → WAITING_FOR_ACK → RETRYING` | Channel discovery, Node lifecycle |
| **NodeStateMachine** | Node lifecycle: `UNINITIALIZED → IDLE → PAIRING → OPERATIONAL → SCANNING` | Individual packet retries |
| **TxManager** | Transmission execution, retry logic, ACK timeout | When to scan (only notifies) |
| **EspNowManager** | Orchestration, state machine coordination | Direct transmission logic |
| **DiscoveryManager** | Scan logic (passive, no task) | Task lifecycle (owned by EspNowManager) |

### 1.2 Data Flow (After Fix)

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         Transmission Failure Flow                        │
└─────────────────────────────────────────────────────────────────────────┘

TxManager.tx_task()
    ↓ (physical transmission fails)
TxStateMachine.on_physical_fail()
    ↓ (send_fail_count_++)
    ├─ if count < MAX_FAILURES → transition to RETRYING
    └─ if count >= MAX_FAILURES → transition to IDLE + notify observer
                                      ↓
                        ITxFailureObserver.on_max_transmission_failures()
                                      ↓
                            EspNowManager.rx_task (via notification)
                                      ↓
                            NodeStateMachine.on_scan_requested()
                                      ↓
                            NodeState: OPERATIONAL → SCANNING
                                      ↓
                            DiscoveryManager.start_scan() (non-blocking)
                                      ↓
                            discovery_task wakes up and scans
                                      ↓
                            rx_task receives CHANNEL_SCAN_RESPONSE
                                      ↓
                            DiscoveryManager.handle_scan_response()
                                      ↓
                            NodeState: SCANNING → OPERATIONAL

```

### 1.3 Component Diagram

```
┌──────────────────────────────────────────────────────────────────────┐
│                         EspNowManager                                 │
│  ┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐ │
│  │ NodeStateMachine│    │  discovery_task  │    │    rx_task      │ │
│  │  - UNINITIALIZED│    │  (4KB stack)     │    │  (6KB stack)    │ │
│  │  - IDLE         │    │                  │    │                 │ │
│  │  - PAIRING      │    │  Waits for:      │    │  Handles:       │ │
│  │  - OPERATIONAL  │    │  - start_scan()  │    │  - RX packets   │ │
│  │  - SCANNING     │    │  - stop_scan()   │    │  - Notifications│ │
│  └────────┬────────┘    └────────┬─────────┘    └────────┬────────┘ │
│           │                      │                       │           │
│           │ notifies             │ wakes up              │ calls     │
│           ↓                      ↓                       ↓           │
│  ┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐ │
│  │TxManager        │    │DiscoveryManager  │    │MessageRouter    │ │
│  │  - queue_packet │    │  - scan()        │    │  - handle_packet│ │
│  │  - notify_fail  │    │  - start_scan()  │    │                 │ │
│  │                 │    │  - handle_response│   │                 │ │
│  └────────┬────────┘    └──────────────────┘    └─────────────────┘ │
│           │                                                         │
│           │ ITxFailureObserver (callback)                           │
│           ↓                                                         │
│  ┌─────────────────┐                                                │
│  │TxStateMachine   │                                                │
│  │  - IDLE         │                                                │
│  │  - WAITING_ACK  │                                                │
│  │  - RETRYING     │                                                │
│  │  (SCANNING ❌)  │                                                │
│  └─────────────────┘                                                │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 2. Interface Changes

### 2.1 NEW: `i_tx_failure_observer.hpp`

**Rationale:** Separate transmission failure notifications from channel observer to maintain SRP.

```cpp
// include/interfaces/i_tx_failure_observer.hpp
#pragma once

#include <cstdint>

/**
 * @interface ITxFailureObserver
 * @brief Observer interface for transmission failure notifications.
 * 
 * Implemented by EspNowManager to receive notifications when TxManager
 * exhausts all retry attempts for a packet.
 */
class ITxFailureObserver
{
public:
    virtual ~ITxFailureObserver() = default;

    /**
     * @brief Called when MAX_FAILURES is reached for a packet.
     * 
     * This is a notification only - the observer decides what action to take.
     * Typically triggers NodeState transition to SCANNING.
     * 
     * @note Called from TxManager task context (thread-safe)
     */
    virtual void on_max_transmission_failures() = 0;
};
```

---

### 2.2 MODIFY: `i_tx_manager.hpp`

**Changes:**
- Add `set_observer()` method
- Add thread-safe observer management

```cpp
// include/interfaces/i_tx_manager.hpp
#pragma once

#include <cstdint>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "espnow_types.hpp"
#include "i_tx_failure_observer.hpp"  // NEW INCLUDE

class ITxManager
{
public:
    virtual ~ITxManager() = default;

    virtual esp_err_t init(uint32_t stack_size, UBaseType_t priority) = 0;
    virtual esp_err_t deinit() = 0;
    virtual esp_err_t queue_packet(const DecodedTxPacket &packet) = 0;
    virtual void notify_physical_fail() = 0;
    virtual void notify_link_alive() = 0;
    virtual void notify_logical_ack() = 0;
    virtual void notify_scanning() = 0;
    virtual TaskHandle_t get_task_handle() const = 0;

    // NEW METHOD: Thread-safe observer registration
    /**
     * @brief Set the failure observer.
     * @param observer Observer to notify on MAX_FAILURES. Can be nullptr.
     * @note Thread-safe: can be called from any task.
     */
    virtual void set_observer(ITxFailureObserver* observer) = 0;
};
```

---

### 2.3 MODIFY: `i_discovery_manager.hpp`

**Changes:**
- Add task lifecycle methods
- Add synchronization primitives

```cpp
// include/interfaces/i_discovery_manager.hpp
#pragma once

#include <cstdint>
#include "esp_err.h"

#include "espnow_types.hpp"
#include "i_channel_observer.hpp"

class IDiscoveryManager
{
public:
    virtual ~IDiscoveryManager() = default;

    /**
     * @brief Initialize the discovery manager.
     * @param id This node's ID.
     * @param type This node's type.
     * @param observer Observer for scan events. Can be nullptr.
     * @return ESP_OK on success.
     */
    virtual esp_err_t init(NodeId id, NodeType type, IChannelObserver* observer = nullptr) = 0;

    /**
     * @brief Deinitialize and stop the discovery task.
     * @note Blocks until task exits (up to 1s timeout).
     */
    virtual esp_err_t deinit() = 0;

    /**
     * @brief Start an asynchronous channel scan.
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_STATE if scan already in progress or not initialized.
     * @note Non-blocking: signals the internal task to start.
     */
    virtual esp_err_t start_scan() = 0;

    /**
     * @brief Stop an ongoing scan.
     * @return ESP_OK on success.
     * @return ESP_ERR_INVALID_STATE if no scan in progress.
     * @note Non-blocking: signals the internal task to stop.
     */
    virtual esp_err_t stop_scan() = 0;

    /**
     * @brief Check if a scan is currently in progress.
     * @return true if scanning, false otherwise.
     * @note Thread-safe.
     */
    virtual bool is_scanning() const = 0;

    /**
     * @brief Handle incoming CHANNEL_SCAN_RESPONSE packets.
     * @param decoded The decoded response packet.
     * @note Called from rx_task context. Thread-safe.
     */
    virtual void handle_scan_response(const DecodedPacket& decoded) = 0;

    /**
     * @brief Set the WiFi channel for scanning.
     * @param channel Primary channel (1-14).
     * @note Should be called before start_scan().
     */
    virtual void set_channel(uint8_t channel) = 0;
};
```

---

## 3. Implementation Changes

### 3.1 MODIFY: `tx_state_machine.hpp` and `.cpp`

**Changes:**
- Remove `TxState::SCANNING`
- Remove `on_scan_requested()`
- Change `on_physical_fail()` to reset after MAX_FAILURES

```cpp
// include/tx_state_machine.hpp
#pragma once

#include <cstdint>
#include <optional>

#include "espnow_types.hpp"

/**
 * @brief Transmission states for individual packets.
 * 
 * Note: SCANNING was removed - channel discovery is now handled
 * by NodeStateMachine via ITxFailureObserver callback.
 */
enum class TxState
{
    IDLE,            ///< No active transmission
    WAITING_FOR_ACK, ///< Physical send success, waiting for logical ACK
    RETRYING,        ///< Waiting before retransmission attempt
    COUNT            ///< Number of states (for validation)
};

class TxStateMachine
{
public:
    TxStateMachine();
    void reset();
    void set_pending_ack(const PendingAck& pending_ack);
    
    TxState on_tx_success(bool requires_ack);
    TxState on_ack_received();
    TxState on_link_alive();
    TxState on_ack_timeout();
    
    /**
     * @brief Handle physical transmission failure.
     * @return true if MAX_FAILURES reached (observer should be notified).
     * @return false if still retrying.
     */
    bool on_physical_fail();  // CHANGED: returns bool instead of TxState
    
    TxState on_max_retries();
    TxState get_state() const { return current_state_; }
    uint8_t get_fail_count() const { return send_fail_count_; }

private:
    TxState current_state_;
    std::optional<PendingAck> pending_ack_;
    uint8_t send_fail_count_;
};
```

```cpp
// src/tx_state_machine.cpp
bool TxStateMachine::on_physical_fail()
{
    send_fail_count_++;

    if (send_fail_count_ >= MAX_FAILURES) {
        // Reset for next packet
        send_fail_count_ = 0;
        if (pending_ack_) {
            pending_ack_.reset();
        }
        current_state_ = TxState::IDLE;  // CHANGED: was SCANNING
        return true;  // Signal: observer should be notified
    }
    
    current_state_ = TxState::RETRYING;
    return false;  // Still retrying, no notification needed
}

// REMOVE: TxState TxStateMachine::on_scan_requested()
```

---

### 3.2 MODIFY: `tx_manager.hpp`

**Changes:**
- Add observer member with mutex protection
- Add observer notification logic

```cpp
// include/tx_manager.hpp
#pragma once

#include <atomic>
#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "i_tx_manager.hpp"
#include "i_tx_state_machine.hpp"
#include "i_discovery_manager.hpp"
#include "i_hal_wifi.hpp"
#include "i_hal_timer.hpp"
#include "i_hal_freertos.hpp"
#include "i_message_codec.hpp"
#include "i_tx_failure_observer.hpp"  // NEW INCLUDE

#include "tx_state_machine.hpp"
#include "protocol_types.hpp"

class TxManager : public ITxManager
{
public:
    TxManager(
        ITxStateMachine& fsm,
        IDiscoveryManager& scanner,
        IWiFiHAL& hal_wifi,
        IFreeRTOSHAL& hal_freertos,
        IMessageCodec& codec,
        uint32_t ack_timeout_ms);
    
    ~TxManager() override;

    // ITxManager interface
    esp_err_t init(uint32_t stack_size, UBaseType_t priority) override;
    esp_err_t deinit() override;
    esp_err_t queue_packet(const DecodedTxPacket& packet) override;
    void notify_physical_fail() override;
    void notify_link_alive() override;
    void notify_logical_ack() override;
    void notify_scanning() override;
    TaskHandle_t get_task_handle() const override;
    
    // NEW: Thread-safe observer registration
    void set_observer(ITxFailureObserver* observer) override;

private:
    static void tx_task_func(void* arg);
    void tx_task();
    void handle_notifications(uint32_t notifications, bool& should_stop);
    void handle_esp_now_send_errors(esp_err_t error);
    esp_err_t encode_and_send(const DecodedTxPacket& structured_packet, TxPacket& raw_packet);
    void process_pending_packet();
    void reset_pending();

    // Members
    ITxStateMachine& fsm_;
    IDiscoveryManager& scanner_;
    IWiFiHAL& hal_wifi_;
    IFreeRTOSHAL& freertos_hal_;
    IMessageCodec& codec_;
    uint32_t ack_timeout_ms_;

    TaskHandle_t task_handle_ = nullptr;
    QueueHandle_t tx_queue_ = nullptr;
    TimerHandle_t ack_timeout_timer_ = nullptr;
    
    // NEW: Thread-safe observer
    ITxFailureObserver* observer_ = nullptr;
    SemaphoreHandle_t observer_mutex_ = nullptr;  // Protects observer_
    
    std::atomic<bool> is_initialized_{false};
    std::atomic<bool> is_scanning_{false};
    
    // Pending packet state (protected by task ownership - only accessed in tx_task)
    std::optional<PendingAck> pending_ack_;
    uint8_t sequence_number_ = 0;
};
```

---

### 3.3 MODIFY: `tx_manager.cpp`

**Changes:**
- Implement observer notification
- Add mutex protection for observer access

```cpp
// src/tx_manager.cpp

// ADD in constructor
TxManager::TxManager(...)
    : fsm_(fsm)
    , scanner_(scanner)
    , hal_wifi_(hal_wifi)
    , freertos_hal_(hal_freertos)
    , codec_(codec)
    , ack_timeout_ms_(ack_timeout_ms)
{
    // Create mutex for observer protection
    observer_mutex_ = freertos_hal_.mutex_create();
}

// ADD: Observer setter implementation
void TxManager::set_observer(ITxFailureObserver* observer)
{
    if (observer_mutex_ == nullptr) {
        return;  // Not initialized yet
    }
    
    if (freertos_hal_.semaphore_take(observer_mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "set_observer: timeout waiting for mutex");
        return;
    }
    
    observer_ = observer;
    freertos_hal_.semaphore_give(observer_mutex_);
}

// MODIFY: notify_physical_fail to check FSM result
void TxManager::notify_physical_fail()
{
    if (task_handle_ == nullptr) {
        return;
    }
    
    // Check if MAX_FAILURES was reached
    bool max_failures_reached = fsm_.on_physical_fail();
    
    if (max_failures_reached) {
        // Notify observer to trigger NodeState transition
        if (observer_mutex_ != nullptr && 
            freertos_hal_.semaphore_take(observer_mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
            
            if (observer_ != nullptr) {
                observer_->on_max_transmission_failures();
            }
            
            freertos_hal_.semaphore_give(observer_mutex_);
        }
    }
    
    // Always notify task to process the state change
    freertos_hal_.task_notify(task_handle_, NOTIFY_PHYSICAL_FAIL, eSetBits);
}

// MODIFY: tx_task to remove SCANNING case
void TxManager::tx_task()
{
    DecodedTxPacket structured_packet;
    TxPacket raw_packet;
    bool should_stop = false;
    uint32_t notifications = 0;

    ESP_LOGI(TAG, "TX Manager task started.");

    while (!should_stop) {
        TxState current_state = fsm_.get_state();

        // 1. Process notifications
        if (freertos_hal_.task_notify_wait(0, 0xFFFFFFFF, &notifications, 0) == pdTRUE) {
            handle_notifications(notifications, should_stop);
            if (should_stop) {
                break;
            }
            current_state = fsm_.get_state();  // Refresh after notification handling
        }

        // 2. State machine
        switch (current_state) {
            case TxState::IDLE:
                if (freertos_hal_.queue_receive(tx_queue_, &structured_packet, pdMS_TO_TICKS(100)) == pdTRUE) {
                    // ... existing packet processing ...
                }
                break;

            case TxState::WAITING_FOR_ACK:
                // ... existing wait logic ...
                break;

            case TxState::RETRYING:
                // ... existing retry logic ...
                break;

            // REMOVED: case TxState::SCANNING:
        }
    }

    // Cleanup
    ESP_LOGI(TAG, "TX Manager task exiting.");
    freertos_hal_.timer_delete(ack_timeout_timer_);
    freertos_hal_.task_delete(nullptr);
}

// MODIFY: handle_notifications to remove SCANNING case
void TxManager::handle_notifications(uint32_t notifications, bool& should_stop)
{
    if ((notifications & NOTIFY_LINK_ALIVE) == NOTIFY_LINK_ALIVE) {
        fsm_.on_link_alive();
    }
    
    // REMOVED: if ((notifications & NOTIFY_SCANNING) == NOTIFY_SCANNING)
    
    if ((notifications & NOTIFY_PHYSICAL_FAIL) == NOTIFY_PHYSICAL_FAIL) {
        // State transition already handled in notify_physical_fail()
        // Just refresh state here
    }
    
    if ((notifications & NOTIFY_LOGICAL_ACK) == NOTIFY_LOGICAL_ACK) {
        fsm_.on_ack_received();
        freertos_hal_.timer_stop(ack_timeout_timer_, pdMS_TO_TICKS(10));
    }
    
    if ((notifications & NOTIFY_ACK_TIMEOUT) == NOTIFY_ACK_TIMEOUT) {
        fsm_.on_ack_timeout();
    }
    
    if ((notifications & NOTIFY_STOP) == NOTIFY_STOP) {
        should_stop = true;
    }
}
```

---

### 3.4 MODIFY: `discovery_manager.hpp`

**Changes:**
- Add task handle and synchronization primitives
- Use atomic for thread-safe flags

```cpp
// include/discovery_manager.hpp
#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "i_discovery_manager.hpp"
#include "i_hal_wifi.hpp"
#include "i_hal_freertos.hpp"
#include "i_message_codec.hpp"
#include "i_channel_observer.hpp"
#include "protocol_types.hpp"

class DiscoveryManager : public IDiscoveryManager
{
public:
    DiscoveryManager(
        IWiFiHAL& hal_wifi,
        IMessageCodec& codec,
        IFreeRTOSHAL& hal_freertos);
    
    ~DiscoveryManager() override;

    // IDiscoveryManager interface
    esp_err_t init(NodeId id, NodeType type, IChannelObserver* observer = nullptr) override;
    esp_err_t deinit() override;
    esp_err_t start_scan() override;
    esp_err_t stop_scan() override;
    bool is_scanning() const override;
    void handle_scan_response(const DecodedPacket& decoded) override;
    void set_channel(uint8_t channel) override;

private:
    static void discovery_task_func(void* arg);
    void discovery_task();
    esp_err_t scan_channel(uint8_t channel);
    void send_scan_probe();

    // Dependencies
    IWiFiHAL& hal_wifi_;
    IMessageCodec& codec_;
    IFreeRTOSHAL& hal_freertos_;

    // Configuration
    NodeId node_id_ = 0;
    NodeType node_type_ = ReservedTypes::UNKNOWN;
    IChannelObserver* observer_ = nullptr;
    uint8_t current_channel_ = 1;

    // Task management
    TaskHandle_t task_handle_ = nullptr;
    std::atomic<bool> is_initialized_{false};
    std::atomic<bool> is_scanning_{false};
    std::atomic<bool> should_stop_scan_{false};

    // Synchronization for scan response
    std::atomic<bool> response_received_{false};
    std::atomic<uint8_t> found_channel_{0};
    SemaphoreHandle_t response_semaphore_ = nullptr;  // NEW: for rx_task → discovery_task sync
};
```

---

### 3.5 MODIFY: `discovery_manager.cpp`

**Changes:**
- Implement task-based async scanning
- Use proper synchronization primitives

```cpp
// src/discovery_manager.cpp

DiscoveryManager::DiscoveryManager(...)
    : hal_wifi_(hal_wifi)
    , codec_(codec)
    , hal_freertos_(hal_freertos)
{
}

esp_err_t DiscoveryManager::init(NodeId id, NodeType type, IChannelObserver* observer)
{
    if (is_initialized_.load()) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    node_id_ = id;
    node_type_ = type;
    observer_ = observer;
    
    // Create response semaphore (binary semaphore for task sync)
    response_semaphore_ = hal_freertos_.semaphore_create();
    if (response_semaphore_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create response semaphore");
        return ESP_FAIL;
    }

    // Create discovery task
    BaseType_t ret = hal_freertos_.task_create(
        discovery_task_func,
        "discovery_task",
        4096,  // 4KB stack - measured usage: ~2.8KB
        this,
        8,     // Priority: lower than rx_task (10) and tx_task (9)
        &task_handle_);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create discovery task: %d", ret);
        hal_freertos_.semaphore_delete(response_semaphore_);
        response_semaphore_ = nullptr;
        return ESP_FAIL;
    }

    is_initialized_.store(true);
    ESP_LOGI(TAG, "Discovery Manager initialized.");
    return ESP_OK;
}

esp_err_t DiscoveryManager::deinit()
{
    if (!is_initialized_.load()) {
        return ESP_OK;  // Idempotent
    }

    // Signal task to stop
    should_stop_scan_.store(true);
    
    if (task_handle_ != nullptr) {
        // Wake up task if it's waiting
        hal_freertos_.task_notify(task_handle_, 1, eSetBits);
        
        // Wait for task to exit (up to 1s)
        uint16_t timeout_ms = 1000;
        uint8_t delay_ms = 10;
        while (timeout_ms > 0) {
            if (!is_initialized_.load()) {
                break;
            }
            hal_freertos_.task_delay(pdMS_TO_TICKS(delay_ms));
            timeout_ms -= delay_ms;
        }
        
        if (is_initialized_.load()) {
            ESP_LOGW(TAG, "Discovery task did not terminate gracefully");
            hal_freertos_.task_suspend(task_handle_);
            hal_freertos_.task_delete(task_handle_);
        }
        task_handle_ = nullptr;
    }

    // Cleanup
    if (response_semaphore_ != nullptr) {
        hal_freertos_.semaphore_delete(response_semaphore_);
        response_semaphore_ = nullptr;
    }

    is_scanning_.store(false);
    should_stop_scan_.store(false);
    observer_ = nullptr;

    ESP_LOGI(TAG, "Discovery Manager deinitialized.");
    return ESP_OK;
}

esp_err_t DiscoveryManager::start_scan()
{
    if (!is_initialized_.load()) {
        ESP_LOGE(TAG, "start_scan: not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (is_scanning_.load()) {
        ESP_LOGW(TAG, "start_scan: already scanning");
        return ESP_ERR_INVALID_STATE;
    }

    // Reset state for new scan
    response_received_.store(false);
    found_channel_.store(0);
    should_stop_scan_.store(false);
    is_scanning_.store(true);

    // Wake up discovery task
    hal_freertos_.task_notify(task_handle_, 1, eSetBits);

    ESP_LOGI(TAG, "Scan started");
    return ESP_OK;
}

esp_err_t DiscoveryManager::stop_scan()
{
    if (!is_scanning_.load()) {
        return ESP_ERR_INVALID_STATE;
    }

    should_stop_scan_.store(true);
    is_scanning_.store(false);

    // Wake up task so it can check should_stop_scan_
    hal_freertos_.task_notify(task_handle_, 1, eSetBits);

    ESP_LOGI(TAG, "Scan stopped");
    return ESP_OK;
}

bool DiscoveryManager::is_scanning() const
{
    return is_scanning_.load();
}

void DiscoveryManager::handle_scan_response(const DecodedPacket& decoded)
{
    if (!is_scanning_.load()) {
        return;  // Not scanning, ignore response
    }

    if (decoded.header.msg_type == MessageType::CHANNEL_SCAN_RESPONSE) {
        // Store response data
        response_received_.store(true);
        found_channel_.store(decoded.header.sender_node_id);

        // Wake up discovery_task if it's waiting on semaphore
        if (response_semaphore_ != nullptr) {
            hal_freertos_.semaphore_give_from_isr(response_semaphore_);
        }

        ESP_LOGI(TAG, "Scan response received on channel %d", 
                 static_cast<int>(decoded.header.sender_node_id));
    }
}

void DiscoveryManager::set_channel(uint8_t channel)
{
    current_channel_ = channel;
}

void DiscoveryManager::discovery_task_func(void* arg)
{
    static_cast<DiscoveryManager*>(arg)->discovery_task();
    vTaskDelete(NULL);
}

void DiscoveryManager::discovery_task()
{
    ESP_LOGI(TAG, "Discovery task started");

    while (!should_stop_scan_.load()) {
        // Wait for scan start signal (timeout allows checking should_stop_scan_)
        if (hal_freertos_.task_notify_wait(0, 0xFFFFFFFF, nullptr, pdMS_TO_TICKS(1000)) != pdTRUE) {
            continue;  // Timeout, check should_stop_scan_ and loop
        }

        if (should_stop_scan_.load()) {
            break;
        }

        // Perform sequential channel scan
        ESP_LOGI(TAG, "Starting channel scan");
        
        bool found = false;
        for (uint8_t channel = 1; channel <= 14 && !should_stop_scan_.load(); channel++) {
            ESP_LOGD(TAG, "Scanning channel %d", channel);
            
            hal_wifi_.hal_wifi_set_channel(channel);
            
            // Send probe and wait for response
            send_scan_probe();
            
            // Wait for response with timeout (SCAN_CHANNEL_TIMEOUT_MS)
            if (hal_freertos_.semaphore_take(response_semaphore_, 
                                              pdMS_TO_TICKS(SCAN_CHANNEL_TIMEOUT_MS)) == pdTRUE) {
                
                if (response_received_.load()) {
                    found = true;
                    uint8_t found_ch = found_channel_.load();
                    
                    ESP_LOGI(TAG, "Found hub on channel %d", found_ch);
                    
                    if (observer_ != nullptr) {
                        observer_->on_channel_found_cb(found_ch);
                    }
                    break;
                }
            }
        }

        if (!found && !should_stop_scan_.load()) {
            ESP_LOGW(TAG, "Scan completed - no hub found");
            if (observer_ != nullptr) {
                observer_->on_scan_failed_cb();
            }
        }

        is_scanning_.store(false);
    }

    ESP_LOGI(TAG, "Discovery task exiting");
    is_initialized_.store(false);
}

void DiscoveryManager::send_scan_probe()
{
    // ... existing probe sending logic ...
}

esp_err_t DiscoveryManager::scan_channel(uint8_t channel)
{
    // ... existing channel scan logic (now called from discovery_task) ...
    return ESP_OK;
}
```

---

### 3.6 MODIFY: `espnow_manager.cpp`

**Changes:**
- Implement `ITxFailureObserver`
- Wire up observer pattern
- Handle failure notifications

```cpp
// ADD: EspNowManager now implements ITxFailureObserver
class EspNowManager : public IEspNowManager, public IChannelObserver, public ITxFailureObserver
{
    // ... existing members ...

    // ITxFailureObserver interface
    void on_max_transmission_failures() override;
};

// MODIFY: init() to wire up observer
esp_err_t EspNowManager::init(const EspNowConfig &config)
{
    // ... existing init code ...

    // NEW: Wire up failure observer
    if (tx_manager_ != nullptr) {
        tx_manager_->set_observer(this);
    }

    // ... rest of init ...
}

// ADD: Failure notification handler
void EspNowManager::on_max_transmission_failures()
{
    // Called from TxManager task context
    // Notify rx_task to transition NodeState
    if (rx_task_handle_ != nullptr) {
        hal_freertos_->task_notify(rx_task_handle_, NOTIFY_SCANNING, eSetBits);
    }
}

// MODIFY: handle_notifications to process NOTIFY_SCANNING
void EspNowManager::handle_notifications(uint32_t notifications, bool &should_stop)
{
    if ((notifications & NOTIFY_SCANNING) == NOTIFY_SCANNING) {
        // Transition NodeState to SCANNING
        node_fsm_->on_scan_requested();
        
        // Start discovery scan
        if (scanner_ != nullptr && !scanner_->is_scanning()) {
            scanner_->start_scan();
        }
    }
    
    // ... rest of existing notification handling ...
}
```

---

### 3.7 MODIFY: `espnow_types.hpp`

**Changes:**
- Add discovery task config to `EspNowConfig`

```cpp
struct EspNowConfig
{
    // ... existing fields ...

    uint32_t stack_size_rx_task;          ///< Default: 6144 (6KB)
    uint32_t stack_size_tx_task;          ///< Default: 4096 (4KB)
    uint32_t stack_size_discovery_task;   ///< NEW: Default: 4096 (4KB)

    UBaseType_t priority_rx_task;         ///< Default: 10
    UBaseType_t priority_tx_task;         ///< Default: 9
    UBaseType_t priority_discovery_task;  ///< NEW: Default: 8

    // ... rest of config ...

    EspNowConfig()
        : node_id(ReservedIds::HUB)
        , node_type(ReservedTypes::UNKNOWN)
        , app_rx_queue(nullptr)
        , wifi_channel(DEFAULT_WIFI_CHANNEL)
        , ack_timeout_ms(DEFAULT_ACK_TIMEOUT_MS)
        , heartbeat_interval_ms(DEFAULT_HEARTBEAT_INTERVAL_MS)
        , channel_monitor_interval_ms(DEFAULT_CHANNEL_MONITOR_INTERVAL_MS)
        , stack_size_rx_task(6144)        // CHANGED: 4096 → 6144
        , stack_size_tx_task(4096)
        , stack_size_discovery_task(4096) // NEW
        , priority_rx_task(10)
        , priority_tx_task(9)
        , priority_discovery_task(8)      // NEW
        , rx_queue_length(30)
        , tx_queue_length(20)
    {
    }
};
```

---

## 4. Thread Safety Summary

| Shared Resource | Protection Mechanism | Access Pattern |
|-----------------|---------------------|----------------|
| `observer_` (TxManager) | `observer_mutex_` (Semaphore) | Write: `set_observer()`, Read: `notify_physical_fail()` |
| `response_received_` (DiscoveryManager) | `std::atomic<bool>` | Write: `handle_scan_response()`, Read: `discovery_task()` |
| `found_channel_` (DiscoveryManager) | `std::atomic<uint8_t>` | Write: `handle_scan_response()`, Read: `discovery_task()` |
| `is_scanning_` (DiscoveryManager) | `std::atomic<bool>` | All tasks |
| `should_stop_scan_` (DiscoveryManager) | `std::atomic<bool>` | All tasks |
| `response_semaphore_` | FreeRTOS Binary Semaphore | Give: `handle_scan_response()`, Take: `discovery_task()` |

---

## 5. Testing Strategy

### 5.1 Unit Tests (Host)

#### `test_tx_state_machine.cpp`

```cpp
TEST_F(TxStateMachineTest, OnPhysicalFailReturnsTrueAtMaxFailures)
{
    TxStateMachine fsm;
    
    // Trigger MAX_FAILURES - 1 failures
    for (int i = 0; i < MAX_FAILURES - 1; i++) {
        EXPECT_FALSE(fsm.on_physical_fail());
        EXPECT_EQ(fsm.get_state(), TxState::RETRYING);
    }
    
    // MAX_FAILURES-th failure should return true and reset to IDLE
    EXPECT_TRUE(fsm.on_physical_fail());
    EXPECT_EQ(fsm.get_state(), TxState::IDLE);
    EXPECT_EQ(fsm.get_fail_count(), 0);  // Reset
}
```

#### `test_tx_manager.cpp`

```cpp
TEST_F(TxManagerTest, ObserverNotifiedOnMaxFailures)
{
    // Setup
    MockTxFailureObserver observer;
    tx_manager->set_observer(&observer);
    
    // Expect notification
    EXPECT_CALL(observer, on_max_transmission_failures()).Times(1);
    
    // Trigger MAX_FAILURES
    for (int i = 0; i < MAX_FAILURES; i++) {
        tx_manager->notify_physical_fail();
    }
    
    // Verify notification occurred
    ::testing::Mock::VerifyAndClearExpectations(&observer);
}

TEST_F(TxManagerTest, SetObserverThreadSafe)
{
    // Multiple threads setting observer concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([this]() {
            MockTxFailureObserver obs;
            tx_manager->set_observer(&obs);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Should not crash (thread-safe)
}
```

#### `test_discovery_manager.cpp`

```cpp
TEST_F(DiscoveryManagerTest, StartScanWakesTask)
{
    // Init
    discovery_manager->init(node_id, node_type, &observer);
    
    // Start scan
    EXPECT_EQ(discovery_manager->start_scan(), ESP_OK);
    EXPECT_TRUE(discovery_manager->is_scanning());
    
    // Wait for task to process
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Verify observer was notified
    EXPECT_CALL(observer, on_scan_started_cb()).Times(1);
}

TEST_F(DiscoveryManagerTest, HandleScanResponseThreadSafe)
{
    // Simulate rx_task calling handle_scan_response while discovery_task is scanning
    discovery_manager->init(node_id, node_type, &observer);
    discovery_manager->start_scan();
    
    // Concurrent calls from multiple "rx_task" threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([this]() {
            DecodedPacket packet = create_mock_response();
            discovery_manager->handle_scan_response(packet);
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Should complete without race conditions
    EXPECT_TRUE(discovery_manager->response_received_.load());
}
```

#### `test_espnow_manager.cpp`

```cpp
TEST_F(EspNowManagerTest, OnMaxFailuresTransitionsToScanning)
{
    init_operational_sut();
    
    // Simulate TxManager notification
    sut_->on_max_transmission_failures();
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
    
    // Verify NodeState transition
    EXPECT_EQ(sut_->get_node_state(), NodeState::SCANNING);
    
    // Verify scan started
    EXPECT_CALL(*scanner_, start_scan()).Times(1);
}
```

### 5.2 Integration Tests (On-Target)

```bash
# Test 1: Hub turns off, Node should scan
test_apps/test_scan_on_failure/

# Test 2: Hub changes channel, Node should rediscover
test_apps/test_channel_change/

# Test 3: Multiple nodes scanning simultaneously
test_apps/test_concurrent_scan/
```

---

## 6. Migration Guide

### 6.1 Breaking Changes

1. **`TxState::SCANNING` removed** - Code checking for this state must be updated
2. **`IDiscoveryManager::scan()` removed** - Use `start_scan()` instead (non-blocking)
3. **`ITxManager` now requires observer** - Call `set_observer()` during init

### 6.2 Update Checklist

- [ ] Update all `TxState::SCANNING` checks to use `NodeState::SCANNING`
- [ ] Replace `scanner_->scan()` calls with `scanner_->start_scan()`
- [ ] Add `tx_manager_->set_observer(this)` in `EspNowManager::init()`
- [ ] Update `EspNowConfig` defaults for new stack sizes
- [ ] Add `ITxFailureObserver` interface to `EspNowManager`

### 6.3 Rollback Plan

If issues arise:
1. Revert to previous commit
2. `TxState::SCANNING` behavior is restored
3. No data loss risk (state machines are independent)

---

## 7. Success Criteria

| Criterion | Measurement | Target |
|-----------|-------------|--------|
| Thread Safety | No race conditions in 1000 concurrent test runs | 100% pass |
| State Synchronization | Node enters SCANNING within 100ms of MAX_FAILURES | < 100ms |
| Memory Usage | Stack high water mark within 20% of allocation | < 80% used |
| Test Coverage | Branch coverage for new code | > 90% |
| Integration | Node rediscovers hub on new channel | 100% success |

---

## 8. Implementation Phases

### Phase 1: Foundation (Week 1)
- [ ] Create `ITxFailureObserver` interface
- [ ] Add observer to `TxManager` with mutex protection
- [ ] Write unit tests for observer pattern

### Phase 2: State Machine Cleanup (Week 2)
- [ ] Remove `TxState::SCANNING`
- [ ] Update `TxStateMachine::on_physical_fail()` to return bool
- [ ] Update `TxManager::tx_task()` to remove SCANNING case
- [ ] Write unit tests for new FSM behavior

### Phase 3: Async Discovery (Week 3)
- [ ] Add task to `DiscoveryManager`
- [ ] Implement `start_scan()` / `stop_scan()`
- [ ] Add atomic flags and semaphore synchronization
- [ ] Write unit tests for thread safety

### Phase 4: Integration (Week 4)
- [ ] Wire up `EspNowManager::on_max_transmission_failures()`
- [ ] Update `handle_notifications()` to start scan
- [ ] Integration testing on hardware
- [ ] Performance profiling and stack tuning

---

## 9. Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Race condition in observer | High | Low | Mutex protection, extensive testing |
| Stack overflow (new task) | High | Medium | Conservative stack sizes, monitoring |
| Scan timeout too short | Medium | Medium | Configurable timeout, field testing |
| Backward compatibility | Medium | High | Clear migration guide, rollback plan |

---

**Approved by:** [Pending Review]  
**Implementation Owner:** [TBD]  
**Target Completion:** 4 weeks from approval
