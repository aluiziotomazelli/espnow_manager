# ESP-NOW Manager

[![ESP-IDF Build](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/build.yml/badge.svg)](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/build.yml)
[![Host Tests](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/host_test.yml/badge.svg)](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/host_test.yml)
[![Coverage](https://img.shields.io/badge/coverage-95%25-orange)](https://aluiziotomazelli.github.io/espnow_manager/index.html)

A high-level C++ component for ESP32 devices that provides reliable, structured communication built on top of ESP-NOW (Espressif's low-power, peer-to-peer wireless protocol).

## Overview

ESP-NOW Manager enables seamless wireless communication between ESP32 devices using a **Facade + Decentralized Managers** architecture. It handles peer discovery, pairing, link monitoring, and reliable data transmission automatically.

> **⚠️ Network Topology: Star (Not Mesh)**
> 
> ESP-NOW Manager uses a **star topology** centered around a HUB device:
> - **One HUB required**: All communication flows through a central HUB node
> - **NODEs cannot communicate directly**: NODEs can only talk to the HUB, not to each other
> - **HUB coordinates the network**: The HUB manages peer registration and message routing
> 
> This is **NOT a mesh network** — NODEs do not relay messages for other nodes.

### Typical Use Cases

- **HUB + Sensor Networks**: Central controller collecting data from multiple peripheral sensors
- **Home Automation**: Wireless control of distributed devices without WiFi infrastructure
- **Industrial Monitoring**: Low-power sensor networks with automatic peer management
- **Multi-device Coordination**: Synchronized communication between ESP32 devices

## Key Features

- **Automatic Peer Discovery**: Multi-channel scanning to find and register peers
- **Pairing Protocol**: Simple registration process for new nodes
- **Link Health Monitoring**: Heartbeat-based connection tracking with offline detection
- **Reliable Transmission**: Retry logic with logical ACK/NACK protocol
- **Channel Recovery**: Automatic multi-channel scanning when connection is lost
- **Dual Storage Strategy**: RTC RAM for fast wake-from-deep-sleep, NVS for long-term backup
- **Multi-Channel Operation**: Automatic channel synchronization across peers

## Architecture

ESP-NOW Manager uses a **Facade pattern** with specialized managers for each protocol aspect:

```
┌─────────────────────────────────────────────────────────┐
│                  EspNowManager                          │
│              (Public Facade / RX Task)                  │
└─────────────────────────────────────────────────────────┘
              │              │              │
    ┌─────────┴──────┐ ┌───┴────────┐ ┌──┴──────────┐
    │  TxManager     │ │DiscoveryMgr│ │PairingMgr   │
    │  (Encoding)    │ │(Scanning)  │ │(Registration)│
    └────────────────┘ └────────────┘ └─────────────┘
              │              │              │
    ┌─────────┴──────┐ ┌───┴────────┐ ┌──┴──────────┐
    │ HeartbeatMgr   │ │ChannelMonitor│ │PeerManager │
    │(Link Health)   │ │(WiFi Watch) │ │(Peer DB)   │
    └────────────────┘ └─────────────┘ └─────────────┘
```

### Core Components

| Component | Responsibility |
|-----------|---------------|
| `EspNowManager` | Public API, singleton orchestrator, owns RX task |
| `TxManager` | Centralized encoding, transmission queue, retry logic |
| `TxStateMachine` | Transmission state machine (IDLE / WAITING_FOR_ACK / RETRYING) |
| `DiscoveryManager` | Multi-channel probing and channel discovery |
| `PairingManager` | Node registration and channel synchronization |
| `HeartbeatManager` | Link monitoring and keep-alive generation |
| `ChannelMonitor` | WiFi channel change detection |
| `PeerManager` | Peer database with LRU eviction (max 19 peers) |
| `StorageManager` | Persistence using RTC + NVS dual storage |
| `StatisticsManager` | Per-peer network quality metrics (RSSI, RTT, packet counts) |
| `MessageCodec` | Protocol serialization/deserialization with CRC validation |
| `MessageRouter` | Dispatches protocol packets to specific managers |
| `EspNowDriver` | ESP-NOW initialization and HAL abstraction |
| `NodeStateMachine` | Governs high-level node state transitions |

### Hardware Abstraction

The component uses separated HAL interfaces for fine-grained control and testability:
- **IWiFiHAL**: WiFi channel control
- **IEspNowHAL**: ESP-NOW operations
- **ITimerHAL**: Time services
- **IFreeRTOSHAL**: FreeRTOS services (tasks, queues, semaphores)

## Node States

The `NodeStateMachine` governs the high-level state of the ESP-NOW node:

| State | Description |
|-------|-------------|
| `UNINITIALIZED` | Initial state before `init()` is called |
| `IDLE` | Initialized, no peers — pairing timed out or scan failed. Call `start_pairing()` to retry |
| `PAIRING_SCAN` | Scanning for HUB to initiate pairing |
| `PAIRING` | Actively exchanging pairing messages |
| `OPERATIONAL` | Has peers, normal communication |
| `RECOVERY_SCAN` | Lost connection, scanning to recover |

### Basic State Flow

```
UNINITIALIZED ──init()──> IDLE          (no peers, hub)
              │           └───────────> PAIRING_SCAN  (no peers, node)
              │           └───────────> OPERATIONAL   (has peers)
              │
              │ has peers
              ├───────────────────────> PAIRING
              │                          │
              │ channel found            │ pairing timeout
              ▼                          ▼
        OPERATIONAL <────────────── PAIRING
              │                          │
              │ link lost                │ pairing timeout
              ▼                          ▼
        RECOVERY_SCAN ───────────> IDLE / OPERATIONAL
```

## Requirements

- **Platform**: ESP32 family (ESP32, ESP32-C3, ESP32-S3, etc.)
- **Framework**: ESP-IDF v5.1.1+
- **Language**: C++17
- **Dependencies**:
  - `etl` (Embedded Template Library)
  - `nvs_flash` (Non-Volatile Storage)
  - `esp_wifi` (WiFi and ESP-NOW driver)
  - `esp_timer` (High-resolution timers)
  - FreeRTOS (IDF version)

## Quick Start Guide

### 1. Installation

Add the component to your ESP-IDF project:

```cmake
# CMakeLists.txt
set(EXTRA_COMPONENT_DIRS path/to/espnow_manager)
```

### 2. Basic Initialization

```cpp
#include "espnow_manager.hpp"

// Create application RX queue for receiving DATA/COMMAND messages
QueueHandle_t app_queue = xQueueCreate(30, sizeof(AppMessage));

// Configure the manager
EspNowConfig config;
config.node_id = ReservedIds::HUB;           // This device is the HUB
config.node_type = ReservedTypes::HUB;
config.app_rx_queue = app_queue;
config.wifi_channel = 6;                     // WiFi channel 6
config.heartbeat_interval_ms = 60000;        // 1 minute heartbeats

// Initialize
EspNowManager& manager = EspNowManager::instance();
esp_err_t err = manager.init(config);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize: %s", esp_err_to_name(err));
}
```

### 3. Start Pairing

**As HUB (accepting new nodes):**
```cpp
// Listen for pairing requests for 30 seconds
err = manager.start_pairing(30000);
```

**As NODE (seeking a HUB):**
```cpp
// Broadcast pairing request, wait for HUB response
err = manager.start_pairing(30000);
```

### 4. Send Data

```cpp
// Send sensor data to HUB (node ID 0x01)
SensorReport report = {.temperature = 25.5, .humidity = 60};
err = manager.send_data(
    ReservedIds::HUB,              // Destination: HUB
    0x01,                          // Application-defined payload type
    &report,                       // Payload pointer
    sizeof(report),                // Payload size
    true                           // Require ACK
);

if (err == ESP_OK) {
    ESP_LOGI(TAG, "Data sent successfully");
} else if (err == ESP_ERR_TIMEOUT) {
    ESP_LOGE(TAG, "ACK timeout - peer may be offline");
}
```

### 5. Send Commands

```cpp
// Send command to node (e.g., change reporting interval)
CommandPayload cmd = {.interval_ms = 5000};
err = manager.send_command(
    node_id,                              // Target node ID
    static_cast<PayloadType>(CommandType::SET_REPORT_INTERVAL),
    &cmd,
    sizeof(cmd),
    true                                  // Require ACK
);
```

### 6. Receive Data

```cpp
// Task to receive messages from application queue
void app_task(void* pvParameters)
{
    AppMessage msg;
    while (true) {
        if (xQueueReceive(app_queue, &msg, pdMS_TO_TICKS(1000)) == pdPASS) {
            ESP_LOGI(TAG, "Received from node %d, type %d", 
                     msg.sender_id, msg.payload_type);
            
            // Cast payload to your application struct
            auto* sensor_data = reinterpret_cast<SensorReport*>(msg.payload);
            
            // Send logical ACK if required
            if (msg.requires_ack) {
                manager.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
            }
        }
    }
}
```

### 7. Check Peer Status & Link Health

```cpp
// Check if a specific peer is currently online and responding
if (manager.is_peer_online(NodeId::WATER_TANK)) {
    ESP_LOGI(TAG, "Water tank node is active and reachable");
} else {
    ESP_LOGW(TAG, "Water tank node is OFFLINE or has timed out");
}

// Get all registered peers
auto peers = manager.get_peers();
for (const auto& peer : peers) {
    ESP_LOGI(TAG, "Peer %d, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             peer.node_id,
             peer.mac[0], peer.mac[1], peer.mac[2],
             peer.mac[3], peer.mac[4], peer.mac[5]);
}

// Check for all offline peers in batch
auto offline = manager.get_offline_peers();
if (!offline.empty()) {
    ESP_LOGW(TAG, "%d peers offline", offline.size());
}
```

> **💡 Heartbeat Contract vs Autonomous Emission**
> 
> - **`heartbeat_interval_ms` (Timeout Contract)**: The maximum silence duration expected between messages. Communicated to the Hub during pairing. The Hub marks the node offline after `heartbeat_interval_ms * 3` of total silence.
> - **`enable_heartbeat` (Emission Flag)**: Controls whether the background task autonomously emits periodic `HEARTBEAT` packets. 
>   - Set to `true` (default) on continuously powered nodes that have periods of application silence (e.g. Pump Controller in IDLE).
>   - Set to `false` on high-frequency streaming nodes (e.g. Solar Sensor transmitting telemetry every 500ms) or deep-sleep sensors to eliminate redundant ping traffic while preserving the timeout contract.

## Complete Example: HUB Device

```cpp
#include "espnow_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char* TAG = "HUB";

// Application message queue
static QueueHandle_t app_queue;

// Example payload structure
struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

void hub_task(void* pvParameters)
{
    // Create queue for incoming messages
    app_queue = xQueueCreate(30, sizeof(AppMessage));
    
    // Configure as HUB
    EspNowConfig config;
    config.node_id = ReservedIds::HUB;
    config.node_type = ReservedTypes::HUB;
    config.app_rx_queue = app_queue;
    config.wifi_channel = 6;
    config.heartbeat_interval_ms = 60000;
    
    // Initialize manager
    EspNowManager& manager = EspNowManager::instance();
    ESP_ERROR_CHECK(manager.init(config));
    
    // Start pairing mode (accept new nodes for 60 seconds)
    ESP_ERROR_CHECK(manager.start_pairing(60000));
    
    ESP_LOGI(TAG, "HUB initialized, waiting for sensor data...");
    
    // Main loop: receive sensor data
    AppMessage msg;
    while (true) {
        if (xQueueReceive(app_queue, &msg, pdMS_TO_TICKS(1000)) == pdPASS) {
            if (msg.msg_type == MessageType::DATA) {
                auto* sensor = reinterpret_cast<SensorData*>(msg.payload);
                ESP_LOGI(TAG, "Sensor %d: T=%.1f°C, H=%.1f%%",
                         msg.sender_id, sensor->temperature, sensor->humidity);
                
                // Send ACK
                if (msg.requires_ack) {
                    manager.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
                }
            }
        }
        
        // Check offline peers
        auto offline = manager.get_offline_peers();
        if (!offline.empty()) {
            ESP_LOGW(TAG, "%d sensors offline", offline.size());
        }
    }
}
```

## Complete Example: NODE Device

```cpp
#include "espnow_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char* TAG = "NODE";

static QueueHandle_t app_queue;

struct SensorData {
    float temperature;
    float humidity;
    uint32_t timestamp;
};

void node_task(void* pvParameters)
{
    app_queue = xQueueCreate(30, sizeof(AppMessage));
    
    // Configure as NODE (sensor)
    EspNowConfig config;
    config.node_id = 0x10;                    // Unique node ID
    config.node_type = 0x02;                  // Sensor type
    config.app_rx_queue = app_queue;
    config.wifi_channel = 6;
    config.heartbeat_interval_ms = 60000;     // Send heartbeat every minute
    
    EspNowManager& manager = EspNowManager::instance();
    ESP_ERROR_CHECK(manager.init(config));
    
    // Start pairing to find HUB
    ESP_LOGI(TAG, "Searching for HUB...");
    ESP_ERROR_CHECK(manager.start_pairing(60000));
    
    if (manager.get_node_state() == NodeState::OPERATIONAL) {
        ESP_LOGI(TAG, "Paired with HUB, starting sensor readings...");
    }
    
    // Main loop: read sensors and send data
    SensorData sensor;
    while (true) {
        // Read sensors (replace with actual sensor code)
        sensor.temperature = 25.5;
        sensor.humidity = 60.0;
        sensor.timestamp = esp_timer_get_time() / 1000;
        
        // Send to HUB with ACK
        esp_err_t err = manager.send_data(
            ReservedIds::HUB,
            0x01,  // SENSOR_REPORT type
            &sensor,
            sizeof(sensor),
            true   // Require ACK
        );
        
        if (err == ESP_ERR_TIMEOUT) {
            ESP_LOGE(TAG, "HUB not responding - may be offline");
        }
        
        // Wait 10 seconds between readings
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

## Project Structure

```
espnow_manager/
├── include/
│   ├── interfaces/             # Pure abstract interfaces (for testing)
│   │   ├── i_*.hpp
│   ├── *.hpp                   # Public headers and type definitions
│   └── protocol_types.hpp      # Protocol constants and enums
├── src/
│   ├── espnow_manager.cpp      # Main facade implementation
│   ├── tx_manager.cpp          # Transmission logic
│   ├── discovery_manager.cpp   # Channel scanning
│   ├── heartbeat_manager.cpp   # Link monitoring
│   ├── pairing_manager.cpp     # Pairing protocol
│   ├── message_router.cpp      # Message dispatching
│   ├── message_codec.cpp       # Encoding/decoding
│   ├── storage_manager.cpp     # Persistence logic
│   ├── peer_manager.cpp        # Peer database
│   └── ...                     # Other managers
├── host_test/                  # Host-based unit tests (Linux)
│   ├── test_*/                 # Individual test suites
│   └── CMakeLists.txt
└── test_apps/                  # On-device integration tests
    ├── test_multiple_devices/
    ├── test_multiple_hub/
    └── test_multiple_node/
```

## Building

### Build for ESP32

```bash
# Navigate to a test application
cd test_apps/test_multiple_devices

# Set target (if needed)
idf.py set-target esp32

# Build
. $HOME/esp/esp-idf/export.sh && idf.py build

# Flash and monitor
idf.py -p /dev/ttyUSB0 flash monitor
```

## Testing

The project includes a comprehensive test suite with both host-based and on-device tests.

### Host Tests (Linux)

A complete battery of unit tests runs on Linux using Google Test and Google Mock frameworks. These tests provide fast feedback during development with full code coverage reporting.

For detailed instructions on running host tests, see [host_test/README.md](host_test/README.md).

### On-Device Tests

Integration tests verify multi-device communication scenarios on real ESP32 hardware. These tests validate the complete system under real-world conditions.

Test applications are located in the `test_apps/` directory.

## Constants and Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_PEERS` | 19 | ESP-NOW hardware limit (20 - 1 broadcast) |
| `MAX_PAYLOAD_SIZE` | 232 bytes | Maximum application payload |
| `DEFAULT_ACK_TIMEOUT_MS` | 500 | Logical ACK timeout |
| `DEFAULT_HEARTBEAT_INTERVAL_MS` | 60000 | Default heartbeat interval (1 minute) |
| `MAX_FAILURES` | 3 | Retries before channel scanning |
| `SCAN_CHANNEL_TIMEOUT_MS` | 50 | Time per channel during scan |
| `PAIRING_TIMEOUT_MS` | 60000 | Pairing session timeout |

## Memory and Task Stack Tuning

ESP-NOW Manager creates three dedicated FreeRTOS tasks to handle communication asynchronously, preventing the application's main thread from blocking. The default stack sizes for these tasks are defined in `include/espnow_types.hpp` and have been rigorously tested to ensure stability under extreme stress conditions.

### `app_main` Stack Requirement

When integrating this component, it is highly recommended to increase your `app_main` task stack size (`CONFIG_ESP_MAIN_TASK_STACK_SIZE`) from the ESP-IDF default (typically 3584 or 4096 bytes) to at least **8192 bytes**. 

This is **not** because ESP-NOW Manager uses dynamic allocation for its components (dynamic allocation uses the Heap, not the Stack). Instead, the larger stack is required because:
1. **Wi-Fi and NVS initialization** (`esp_wifi_init`, `nvs_flash_init`) have deep call trees and consume significant stack memory.
2. **`ESP_LOG` / `printf`** routines use large temporary buffers on the stack for string formatting.
3. The component initialization `EspNowManager::init()` cascades through several constructors before returning.

### Task Stack Sizes (High Water Marks)

An aggressive [stack stress test application](test_apps/stack_stress_test/) was used to validate the memory limits. The test scenario involved:
- Node sending data messages with ACK requests every **250 ms**.
- Node and Hub sending heartbeats every **500 ms** (originally tested at 2x send interval).
- Hub artificially changing its Wi-Fi channel every **20 messages** to constantly force the Node to lose connection, triggering intense multi-channel recovery scans and packet loss.

Under this extreme load, the Peak Stack Usage (High Water Mark) was measured (in bytes):

| Task | Default Size | Peak Consumed (Hub) | Peak Consumed (Node) | Minimum Slack (Safety Margin) |
|------|--------------|---------------------|----------------------|-------------------------------|
| `rx_task` | 6144 | ~5080 | ~3772 | ~1.0 KB |
| `tx_task` | 6144 | ~3140 | ~4984 | ~1.1 KB |
| `discovery_task` | 3072 | ~1436 | ~1952 | ~1.1 KB |

*Note: In ESP-IDF, `uxTaskGetStackHighWaterMark` returns the minimum free stack space (slack) in bytes.*

### Tuning Guidelines

The default sizes are calibrated to leave a safe ~1 KB breathing room in the absolute worst-case scenario. However, depending on your application profile, you can adjust these in `espnow_types.hpp`:

- **`discovery_task` (3072)**: Very stable across all scenarios. It only runs during channel scanning and does not need to be changed.
- **`rx_task` (6144)**: A Hub handling a massive volume of incoming messages from dozens of Nodes might push this boundary. If you experience crashes on the Hub during extreme bursts of traffic, consider increasing this.
- **`tx_task` (6144)**: A Node consumes more TX stack because it actively waits for ACKs, manages timeouts, and handles retry queues. If your Node only sends sporadic, non-ACK messages, you could theoretically reduce this, but 6144 is the recommended safe default.

For custom testing with your specific network traffic patterns, you can modify and run the `stack_stress_test` application to find your own system's "sweet spot".

## Error Handling

All functions return `esp_err_t`. Common error codes:

| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Operation successful |
| `ESP_ERR_INVALID_STATE` | Manager not initialized or already in use |
| `ESP_ERR_INVALID_ARG` | Invalid parameters (null pointer, etc.) |
| `ESP_ERR_NOT_FOUND` | Peer not registered |
| `ESP_ERR_TIMEOUT` | ACK timeout or operation timed out |
| `ESP_ERR_NO_MEM` | Memory allocation failed |
| `ESP_FAIL` | General failure |

### Best Practices

```cpp
// Always check return values
esp_err_t err = manager.send_data(dest_id, type, payload, len, true);
if (err != ESP_OK) {
    if (err == ESP_ERR_TIMEOUT) {
        // Handle timeout - peer may be offline
    } else if (err == ESP_ERR_NOT_FOUND) {
        // Handle missing peer - may need to re-pair
    } else {
        ESP_LOGE(TAG, "Send failed: %s", esp_err_to_name(err));
    }
}

// Handle state transitions
NodeState state = manager.get_node_state();
if (state == NodeState::RECOVERY_SCAN) {
    ESP_LOGW(TAG, "Connection lost, scanning for channel...");
}
```

## Advanced Features

### Persistent Storage

The component automatically persists peer configurations using:
- **RTC RAM**: Fast access during operation and wake-from-deep-sleep
- **NVS**: Long-term backup across reboots

No manual intervention required - storage is managed automatically.

### Multi-Channel Operation

When a HUB changes WiFi channels, connected nodes automatically:
1. Detect link failure via missed heartbeats/ACKs
2. Enter `RECOVERY_SCAN` state
3. Scan all channels (1-13) to find the HUB
4. Resume normal operation once channel is found

If the recovery scan fails, the node retries with exponential backoff:
- **Exponential backoff**: 2s, 4s, 8s, 16s, 32s, 64s, 128s, 256s... up to `scan_max_backoff_ms` (defaults to 5 minutes)
- **Persistent recovery**: Retries continue indefinitely at the capped interval while the node has known peers in storage

This retry mechanism allows the node to recover from transient issues (temporary interference, HUB temporarily offline) without requiring manual intervention or getting stuck permanently offline. The backoff duration doubles with each attempt up to the configured cap to avoid overwhelming the wireless medium.

> **Note on Peer Channel Configuration:** All peers are registered with ESP-NOW using **channel 0 (automatic)**. This means peers automatically use whatever channel the WiFi is currently set to. This design choice simplifies channel management — when the HUB changes channels, nodes detect the change via failed transmissions and automatically scan to rediscover the HUB on the new channel. Using fixed channels per peer would require updating all registered peers when the channel changes, adding complexity without benefit since ESP-NOW does not automatically sync peer channels with WiFi channel changes.

### Peer Statistics

The component tracks per-peer network quality metrics automatically:
- **RSSI**: Last received signal strength + exponential moving average
- **RTT**: Round-trip time for logical ACKs (last + EMA)
- **Packet counts**: Received, sent, retries, lost, delivery failures, driver errors

Access statistics via the public API:
```cpp
// Get statistics for a specific peer
PeerStatistics stats;
if (manager.get_peer_stats(node_id, stats)) {
    ESP_LOGI(TAG, "RSSI avg: %d dBm, RTT avg: %lu ms, Packets rx: %lu",
             stats.rssi_avg, (unsigned long)stats.rtt_avg_ms,
             (unsigned long)stats.packets_rx);
}

// Get statistics for all peers
auto all_stats = manager.get_all_peer_stats();
for (const auto& s : all_stats) {
    ESP_LOGI(TAG, "Peer %d: delivery_failures=%lu, retries=%lu",
             s.node_id, (unsigned long)s.delivery_failures,
             (unsigned long)s.retries);
}
```

Statistics are persisted to NVS using dirty counters — data is flushed to storage when thresholds are reached (configurable via `FLUSH_THRESHOLD_*` constants), ensuring metrics survive unexpected resets.

### Deep Sleep Support

Nodes can enter deep sleep and wake up with preserved peer information:
```cpp
// Before deep sleep
esp_sleep_enable_timer_wakeup(60 * 1000000); // Wake in 60 seconds

// After wake-up, peers are restored from RTC RAM
// No re-pairing required
```

### Simultaneous WiFi (AP/STA) + ESP-NOW & Channel Policy

When running ESP-NOW alongside an active WiFi AP or Station (STA) connection:

- **Channel Ownership**: WiFi channel setting is the responsibility of the application layer or WiFi Manager. `EspNowManager` does not alter the hardware WiFi channel upon initialization. `EspNowConfig::wifi_channel` is only used as an initial starting point for scanning in `SCAN` mode.
- **Channel Policy (`ChannelPolicy`)**:
  - `espnow::ChannelPolicy::SCAN` (default): Dynamic channel scanning. Used in standalone ESP-NOW mode (no AP connection). The discovery manager cycles through WiFi channels during discovery probes.
  - `espnow::ChannelPolicy::FIXED`: Fixed channel mode. Used when connected to a WiFi Access Point. The discovery manager skips channel switching and assumes both HUB and NODE operate on the shared AP channel.

**Example Integration (WiFi State Callback):**

```cpp
// In your WiFi Manager state change callback:
void on_wifi_state_changed(WiFiState state)
{
    if (state == WiFiState::CONNECTED) {
        // Connected to AP: Channel is owned by AP link. Disable channel switching scans.
        EspNowManager::instance().set_channel_policy(espnow::ChannelPolicy::FIXED);
    } else if (state == WiFiState::DISCONNECTED) {
        // Disconnected from AP: Fall back to dynamic multi-channel discovery scan.
        EspNowManager::instance().set_channel_policy(espnow::ChannelPolicy::SCAN);
    }
}
```

## Testing Strategy

### Host-Based Testing
- **100% logic testable** on Linux with mocked HALs
- Google Test + Google Mock framework
- Unified coverage reports with lcov

### On-Device Testing
- Integration tests with real ESP32 hardware
- Multi-device communication scenarios
- Stress testing and performance validation

## References

### Documentation
- [ESP-IDF API Reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/index.html)
- [ESP-NOW Protocol Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- [FreeRTOS (IDF Version)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)

### Project Documentation
- [`API.md`](API.md) - Detailed API reference (in progress)
- [`DESIGN.md`](DESIGN.md) - Internal architecture and design decisions
- [`README.md`](README.md) - This file (overview and usage guide)

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

---

**Note**: For detailed API documentation, see [`API.md`](API.md). For internal architecture details, see [`DESIGN.md`](DESIGN.md).
