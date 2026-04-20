# ESP-NOW Manager - Project Context

## Language Policy

**All responses must be in English**, regardless of the language used in the user's input. This is a mandatory requirement.

## Project Overview

**ESP-NOW Manager** is a C++ component for ESP32 devices that provides a high-level, reliable communication layer built on top of ESP-NOW (Espressif's low-power, peer-to-peer wireless protocol). It implements a **Facade + Decentralized Managers** architecture pattern.

### Purpose
- Enables structured ESP-NOW communication between HUB (central controller) and NODE (peripheral sensor) devices
- Provides automatic peer discovery, pairing, and link health monitoring
- Supports multi-channel operation with automatic channel scanning when links are lost
- Offers persistent storage of peer configurations using NVS and RTC memory

### Key Features
- **Dual Storage Strategy**: RTC RAM for fast wake-from-deep-sleep access, NVS for long-term backup
- **Reliable Transmission**: Retry logic with logical ACK/NACK protocol
- **Channel Discovery**: Automatic multi-channel scanning to find lost peers
- **Heartbeat Monitoring**: Link health tracking with offline detection
- **Pairing Protocol**: Automatic node registration and channel synchronization
- **Host-Based Testing**: 100% of logic testable on Linux with mocked HALs

### Architecture Components

| Component | Role |
|-----------|------|
| `EspNowManager` | Public facade, singleton orchestrator, owns RX task |
| `EspNowDriver` | ESP-NOW initialization and ESP-IDF callback registration |
| `MessageRouter` | Dispatches decoded packets to protocol-specific managers |
| `TxManager` | Centralized encoding, transmission queue, retry logic, FSM |
| `TxStateMachine` | Manages transmission states (IDLE / WAITING_FOR_ACK / RETRYING / SCANNING) |
| `DiscoveryManager` | Multi-channel probing and channel discovery |
| `HeartbeatManager` | Link monitoring and keep-alive generation |
| `PairingManager` | Node registration and channel sync |
| `PeerManager` | Peer database with LRU eviction (max 19 peers) |
| `StorageManager` | High-level persistence logic (RTC + NVS) |
| `MessageCodec` | Protocol serialization/deserialization and CRC validation |
| `ChannelMonitor` | Monitors and manages WiFi channel changes |

### Data Flow

**Reception (RX):**
```
ESP-NOW ISR → rx_task → CRC Validate → Header Decode
    → App Messages (DATA/COMMAND) → App Queue
    → Protocol Messages → MessageRouter → Specific Managers
```

**Transmission (TX):**
```
Managers → DecodedTxPacket (struct) → TxManager
    → Encode + CRC + Sequence → TxPacket (wire format) → ESP-NOW HAL
```

## Technologies & Dependencies

- **Target Platform**: ESP32 family (ESP32, ESP32-C3, ESP32-S3, etc.)
- **Framework**: ESP-IDF v5.1.1+
- **Language**: C++17
- **Key Libraries**:
  - `etl` (Embedded Template Library) - type-safe containers
  - `nvs_flash` - non-volatile storage
  - `esp_wifi` - WiFi and ESP-NOW driver
  - `esp_timer` - high-resolution timers
  - FreeRTOS (IDF version) - tasks, queues, semaphores

## Project Structure

```
espnow_manager/
├── CMakeLists.txt              # ESP-IDF component definition
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
│   ├── CMakeLists.txt          # Test aggregation
│   └── coverage_common.cmake   # Coverage configuration
├── test_apps/                  # On-device integration tests
│   ├── test_multiple_devices/
│   ├── test_multiple_hub/
│   └── test_multiple_node/
└── external/
    └── etl/                    # Embedded Template Library
```

## Building and Running

### Prerequisites

1. **ESP-IDF Environment**: ESP-IDF v5.1.1+ installed and exported
   ```bash
   . $HOME/dev/esp/esp-idf/export.sh
   ```

2. **Host Test Dependencies** (Linux only):
   ```bash
   sudo apt-get install libbsd-dev lcov
   ```

### Building for ESP32

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

### Running Host Tests

**Individual Test:**
```bash
cd host_test/test_message_codec
idf.py set-target linux
idf.py build
./build/test_message_codec.elf
```

**All Tests (Unified Coverage):**
```bash
cd host_test
mkdir -p build && cd build
cmake ..
make run_all_tests
# Coverage report: host_test/coverage/index.html
```

**Generate Coverage Only:**
```bash
cd host_test/build
make unified_coverage
```

### Cleaning

```bash
# Clean specific build
idf.py fullclean

# Manual clean (if fullclean fails)
rm -rf build/
```

## Development Conventions

### Code Style

- **Naming**:
  - Functions/Methods: `snake_case()`
  - Variables: `snake_case`
  - Private Members: `snake_case_` (trailing underscore)
  - Files: `snake_case.cpp` / `snake_case.hpp`

- **Header Guards**: Always use `#pragma once`

- **Include Order** (with blank lines between groups):
  1. `#pragma once`
  2. Standard libraries: `#include <cstdint>`
  3. ESP-IDF: `#include "driver/gpio.h"`
  4. Logging setup:
     ```cpp
     #define LOG_LOCAL_LEVEL ESP_LOG_INFO
     #include "esp_log.h"
     ```
  5. Project headers: `#include "my_component.hpp"`

- **Comments**:
  - Doxygen-style for public APIs in headers
  - Simple `//` comments in implementation files
  - All documentation in **English**

- **Logging**:
  ```cpp
  static const char* TAG = "ComponentName";
  ESP_LOGI(TAG, "Info message");
  ESP_LOGE(TAG, "Error message");
  ```

### Error Handling

- All fallible functions return `esp_err_t`
- Always check return values against `ESP_OK`
- Handle errors gracefully with appropriate fallbacks

### Testing Practices

- **Interface-Based Design**: All dependencies injected via interfaces (`I*`)
- **Mocking**: HALs and managers mocked using Google Mock
- **Test Structure**: One test directory per component
- **Coverage**: lcov + genhtml for unified reports

### Architecture Principles

1. **Non-Blocking**: Long-running operations use dedicated FreeRTOS tasks
2. **Queue-Based Communication**: Tasks communicate via queues, not shared state
3. **Single Responsibility**: Each manager handles one protocol aspect
4. **Structured Data**: Pass decoded structs, not raw buffers, between components
5. **Centralized Encoding**: `TxManager` owns all wire-format encoding

## Key Constants & Limits

```cpp
MAX_PEERS = 19                          // ESP-NOW hardware limit (20 - 1 broadcast)
MAX_PAYLOAD_SIZE = 230 bytes            // ESP_NOW_MAX_DATA_LEN - header - CRC
DEFAULT_ACK_TIMEOUT_MS = 500            // Logical ACK timeout
DEFAULT_HEARTBEAT_INTERVAL_MS = 60000   // 1 minute
MAX_FAILURES = 3                        // Retries before channel scanning
SCAN_CHANNEL_TIMEOUT_MS = 50            // Time per channel during scan
```

## Node States

```
UNINITIALIZED → IDLE         (init(), no peers)
UNINITIALIZED → OPERATIONAL  (init(), has peers)
IDLE          → PAIRING      (start_pairing())
PAIRING       → OPERATIONAL  (pairing success)
PAIRING       → IDLE         (pairing timeout)
OPERATIONAL   → SCANNING     (link lost)
SCANNING      → OPERATIONAL  (channel found)
SCANNING      → PAIRING      (no peers found)
```

## Testing Strategy

### Host Tests (Linux)
- **Purpose**: Unit testing business logic with mocked hardware
- **Framework**: Google Test + Google Mock
- **Coverage**: Unified report across all components
- **CI/CD**: Automated on push/PR via GitHub Actions

### On-Device Tests (ESP32)
- **Purpose**: Integration testing with real hardware
- **Location**: `test_apps/`
- **Scenarios**: Multi-device communication, stress testing

## Common Tasks

### Adding a New Manager

1. Create interface in `include/interfaces/i_new_manager.hpp`
2. Create implementation in `src/new_manager.cpp`
3. Register in `CMakeLists.txt` SRCS
4. Add to `EspNowManager` constructor injection
5. Write unit tests in `host_test/test_new_manager/`

### Modifying Protocol Messages

1. Update `protocol_types.hpp` or `protocol_messages.hpp`
2. Update `MessageCodec::encode()` and `decode()`
3. Update all managers that use the modified message
4. Add new test cases to `test_message_codec`

### Debugging

- **Logs**: Use `ESP_LOG*` macros with appropriate levels
- **Task Stacks**: Monitor via `uxTaskGetStackHighWaterMark()`
- **Queues**: Check queue sizes and utilization
- **Coverage**: Use `make unified_coverage` to find untested paths

## References

- [ESP-IDF API Reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/index.html)
- [ESP-NOW Protocol Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)
- [ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)
- [FreeRTOS (IDF Version)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html)
- [Embedded Template Library](https://www.etlcpp.com/)

## See Also

- `README.md` - StorageManager class documentation
- `DESIGN.md` - Internal architecture and design decisions
- `AGENTS.md` - Agent-specific development guidelines
