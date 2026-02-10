# EspNowStorage Class

A persistent storage solution for ESP-NOW peer information and configuration on ESP32 devices.

## Overview

`EspNowStorage` provides reliable storage for ESP-NOW peer configurations using both NVS (Non-Volatile Storage) and RTC (Real-Time Clock) memory. It ensures data integrity through CRC validation and offers efficient data management.

## Key Features

- **Dual Storage**: Uses both NVS (persistent) and RTC (fast) memory
- **Data Integrity**: CRC32 validation to detect data corruption
- **Optimized Operations**: Avoids unnecessary NVS writes when data hasn't changed
- **Automatic Recovery**: Graceful handling of corrupted or missing data
- **Peer Management**: Stores up to `MAX_PERSISTENT_PEERS` peer configurations

## Storage Architecture

### NVS Storage (Persistent)
- Long-term storage across reboots
- Full data structure with CRC validation
- Used as fallback when RTC data is invalid

### RTC Storage (Fast)
- Fast access during operation
- Mirrors NVS data when valid
- Priority over NVS during `load()` operations
- Lost on deep sleep/power cycle

## Data Structure

### PersistentData
```cpp
struct PersistentData {
    uint32_t magic;               // Magic number for validation
    uint8_t version;              // Version for backward compatibility
    uint8_t wifi_channel;         // ESP-NOW channel
    uint8_t num_peers;            // Number of stored peers
    PeerInfo peers[MAX_PERSISTENT_PEERS];  // Peer array
    uint32_t crc;                 // CRC32 for data integrity
};
```

## Peer Information
Each peer stores:
- MAC address (6 bytes)
- Node type (HUB/SENSOR)
- Node ID
- Channel
- Paired status
- Heartbeat interval

## API Reference

### Core Methods

`save()`
```cpp
esp_err_t save(uint8_t wifi_channel, 
               const std::vector<Peer>& peers, 
               bool force_nvs_commit = true);
```
Saves ESP-NOW configuration to storage.
* **Parameters:**
- `wifi_channel`: ESP-NOW communication channel (1-14)
- `peers`: Vector of peer configurations
- `force_nvs_commit`: Force NVS write (false for optimization)
- **Returns:** `ESP_OK` on success, error code on failure

`load()`
```cpp
esp_err_t load(uint8_t& wifi_channel, 
               std::vector<Peer>& peers);
```               
Loads ESP-NOW configuration from storage.
* **Parameters:**
- `wifi_channel`: Output parameter for loaded channel
- `peers`: Output parameter for loaded peers
- **Returns:** `ESP_OK` on success, error code on failure

## Loading Priority
1. RTC Memory: Checked first if valid (magic + CRC)
2. NVS Storage: Used as fallback if RTC invalid
3. Default Values: If both RTC and NVS fail

## Usage Examples

### Basic Usage
```cpp
#include "espnow_storage.hpp"

// Initialize storage
EspNowStorage storage;

// Save configuration
uint8_t channel = 6;
std::vector<EspNowStorage::Peer> peers;
// ... populate peers
esp_err_t err = storage.save(channel, peers);

// Load configuration
uint8_t loaded_channel;
std::vector<EspNowStorage::Peer> loaded_peers;
err = storage.load(loaded_channel, loaded_peers);
```
### Optimized Save
```cpp
// Save with optimization (no NVS write if unchanged)
storage.save(channel, peers, false);
```

### Configuration Constants
```cpp
static constexpr uint32_t ESPNOW_STORAGE_MAGIC = 0x4E565330;  // "NVS0"
static constexpr uint8_t ESPNOW_STORAGE_VERSION = 1;
static constexpr uint8_t MAX_PERSISTENT_PEERS = 16;
```
### Error Handling
| Error Code              | Description                                  |
|-------------------------|----------------------------------------------|
| `ESP_OK`                | Operation successful                         |
| `ESP_ERR_INVALID_ARG`   | Invalid parameters                           |
| `ESP_ERR_INVALID_SIZE`  | Data size mismatch                           |
| `ESP_ERR_NVS_NOT_FOUND` | Storage key not found                        |
| `ESP_FAIL`              | General failure (CRC/integrity check failed) |

### Best Practices
- Initialize Once: Create one instance per application
- Check Return Codes: Always verify esp_err_t return values
- Use Optimization: Set force_nvs_commit=false for frequent unchanged saves
- Handle Errors Gracefully: Implement fallback for failed loads
- Regular Validation: Periodically verify stored data integrity

### Dependencies
- ESP-IDF NVS component
- C++ Standard Library
- Protocol types (protocol_types.hpp)

### Memory Usage
- NVS: ~1KB for full configuration
- RTC: ~512 bytes
- RAM: Variable based on peer count

### Limitations
- Maximum of 16 persistent peers
- Data lost from RTC on deep sleep
- Requires NVS partition in partition table
- C++17 compatible compiler required

### See Also
[ESP-NOW Protocol Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)

[ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)

## Testing
The class includes extensive unit tests covering:

- Basic Operations: Load/save functionality
- Data Validation: CRC, magic, and version checks
- Edge Cases: Empty data, maximum limits
- Recovery Scenarios: Corrupted data handling
- Performance: Memory usage and optimization
- Priority: RTC vs NVS behavior

### How to Run the Tests
To execute the tests on your ESP32 device:
```bash
# Navigate to the unit-test-app directory
cd unit-test-app

# Build the espnow test component
idf.py -T espnow build

# Flash the firmware to your ESP32 and start monitoring
idf.py -T espnow flash monitor
```
Once the device boots, the tests will automatically be displayed in the serial monitor. More information aboute unit tests:
- [ESP-IDF Unit Testing Guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/unit-tests.html)

### Test Environment
Tests run with controlled memory limits using TestMemoryHelper:
* `set_1kb_limits()`: For memory leak detection
* `set_2kb_limits()`: Standard test environment
* `set_4kb_limits()`: For stress testing