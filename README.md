# ESP-NOW Manager (Namespace v1.1.0)

[![ESP-IDF Build](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/build.yml/badge.svg)](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/build.yml)
[![Host Tests](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/host_test.yml/badge.svg)](https://github.com/aluiziotomazelli/espnow_manager/actions/workflows/host_test.yml)
[![Coverage](https://img.shields.io/badge/coverage-95%25-orange)](https://aluiziotomazelli.github.io/espnow_manager/index.html)

A high-level C++ component for ESP32 devices that provides reliable, structured communication built on top of ESP-NOW (Espressif's low-power, peer-to-peer wireless protocol).

## Overview

ESP-NOW Manager (Namespace v1.1.0) enables seamless wireless communication between ESP32 devices using a **Facade + Decentralized Managers** architecture. It handles peer discovery, pairing, link monitoring, and reliable data transmission automatically.

> **⚠️ Network Topology: Star (Not Mesh)**
> 
> ESP-NOW Manager (Namespace v1.1.0) uses a **star topology** centered around a HUB device:
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

ESP-NOW Manager (Namespace v1.1.0) uses a **Facade pattern** with specialized managers for each protocol aspect:

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

### 7. Check Peer Status

```cpp
// Get all registered peers
auto peers = manager.get_peers();
for (const auto& peer : peers) {
    ESP_LOGI(TAG, "Peer %d, MAC: %02x:%02x:%02x:%02x:%02x:%02x",
             peer.node_id,
             peer.mac[0], peer.mac[1], peer.mac[2],
             peer.mac[3], peer.mac[4], peer.mac[5]);
}

// Check for offline peers
auto offline = manager.get_offline_peers();
if (!offline.empty()) {
    ESP_LOGW(TAG, "%d peers offline", offline.size());
}
```

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
- **`SCAN_MAX_RETRIES = 7`** maximum retries
- **Exponential backoff**: 2s, 4s, 8s, 16s, 32s, 64s, 128s (~4m14s total)
- After all retries are exhausted, the node transitions to `IDLE`

This retry mechanism allows the node to recover from transient issues (temporary interference, HUB temporarily offline) without requiring manual intervention. The backoff duration doubles with each attempt to avoid overwhelming the wireless medium.

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
