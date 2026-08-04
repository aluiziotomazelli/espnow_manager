# Implementation Plan: ESP-NOW Heartbeat Telemetry Decoupling

## Goal Description
The `HeartbeatMessage` in the `espnow_manager` component contains a `battery_mv` field. Because the heartbeat is an internal keep-alive mechanism automatically managed by `HeartbeatManager`, fetching the battery voltage directly inside it would create an unwanted hardware dependency. We need a way to populate this field without violating the Single Responsibility Principle (SRP) and Hardware Abstraction Layer (HAL) rules.

## Analysis of Options

There are three primary options to resolve this:

1. **Remove `battery_mv` from `HeartbeatMessage`**
   - *Pros:* Simplifies the transport layer. The application payload (`WaterLevelReport`) already carries comprehensive battery telemetry.
   - *Cons:* If we introduce new node types in the future that only send heartbeats (e.g., simple sensors or routers without an app payload), the Hub won't know their battery level.

2. **Setter Injection (Push from Application)**
   - *Pros:* Keeps `espnow_manager` decoupled. The application acts as the orchestrator: it reads the battery and pushes the value down to `espnow_manager` via a `set_node_telemetry(uint16_t battery_mv)` method. `HeartbeatManager` caches this value for the next automatic heartbeat.
   - *Cons:* The application must remember to push the telemetry update whenever it reads the sensor.

3. **Interface Injection / Callback (Pull from Transport)**
   - *Pros:* True decoupling. `espnow_manager` is given an `ITelemetryProvider` interface during initialization. It calls `provider->get_battery_mv()` precisely when constructing the heartbeat packet.
   - *Cons:* Adds an extra interface and slightly more structural complexity to the initialization phase.

## Recommendation
**Option 2 (Setter Injection)** is the recommended approach. It is the most pragmatic solution: it avoids tight coupling, doesn't require new interfaces, and perfectly aligns with the current architecture where `WaterTankApp` is the central orchestrator.

---

## Proposed Changes (Assuming Option 2)

### `espnow_manager` Component

#### [MODIFY] include/i_heartbeat_manager.hpp
Add the telemetry setter to the interface.
```cpp
    /**
     * @brief Update the node's telemetry for the next heartbeat.
     * @param battery_mv The latest battery voltage reading.
     */
    virtual void set_telemetry(uint16_t battery_mv) = 0;
```

#### [MODIFY] src/heartbeat_manager.hpp & src/heartbeat_manager.cpp
Implement the setter and use the cached value.
```cpp
// In heartbeat_manager.hpp
private:
    uint16_t last_battery_mv_ = 0;

public:
    void set_telemetry(uint16_t battery_mv) override;

// In heartbeat_manager.cpp
void HeartbeatManager::set_telemetry(uint16_t battery_mv)
{
    last_battery_mv_ = battery_mv;
}

void HeartbeatManager::send_heartbeat()
{
    // ... setup ...
    hb.uptime_ms = now_ms;
    hb.rssi = last_rssi_;
    hb.battery_mv = last_battery_mv_; // <--- Use cached value
    // ...
}
```

#### [MODIFY] include/i_espnow_manager.hpp & include/espnow_manager.hpp
Expose the setter through the main ESP-NOW Manager facade.
```cpp
    /**
     * @brief Update the node's telemetry for internal protocol messages.
     * @param battery_mv The latest battery voltage reading.
     */
    virtual void set_node_telemetry(uint16_t battery_mv) = 0;
```
```cpp
// In EspNowManager implementation
void EspNowManager::set_node_telemetry(uint16_t battery_mv)
{
    if (heartbeat_manager_) {
        heartbeat_manager_->set_telemetry(battery_mv);
    }
}
```

---

### `smart-farm-water-tank` Main Project

#### [MODIFY] main/src/water_tank_app.cpp
Push the battery telemetry down to `espnow_manager` after reading it.
```cpp
void WaterTankApp::run()
{
    // ...
    stats_.last_battery_mv = adc_reader_.read_mv();
    logic_.process_battery(stats_.last_battery_mv, stats_.last_battery_percent, stats_.last_battery_state);

    // Push telemetry to transport layer for heartbeats
    espnow_mgr_.set_node_telemetry(stats_.last_battery_mv);

    // ... continue app logic ...
}
```

---

## Verification Plan

### Automated Tests
- Build the `espnow_manager` host tests using Google Test:
  `cd host_test/test_espnow_manager && idf.py build && ctest`
- The mock classes in `host_test_common` may need a quick update if any mocked interfaces (`IEspNowManager`, `IHeartbeatManager`) changed.

### Manual Verification
1. Ensure the `smart-farm-water-tank` builds successfully.
2. Run the firmware on an ESP32. The Hub logs should display heartbeats arriving with non-zero battery readings matching the application telemetry.
