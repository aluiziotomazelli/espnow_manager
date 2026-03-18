# StorageManager Class

A persistent storage solution for ESP-NOW peer information and configuration on ESP32 devices.

## Overview

`StorageManager` provides reliable storage for ESP-NOW peer configurations using both NVS (Non-Volatile Storage) and RTC (Real-Time Clock) memory. It ensures data integrity through CRC validation and offers efficient data management.

## Key Features

- **Dual Storage**: Uses both NVS (persistent) and RTC (fast) memory
- **Data Integrity**: CRC32 validation to detect data corruption
- **Optimized Operations**: Avoids unnecessary NVS writes when data hasn't changed
- **Automatic Recovery**: Graceful handling of corrupted or missing data
- **Peer Management**: Stores up to `MAX_PEERS` peer configurations

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
    PeerInfo peers[MAX_PEERS];  // Peer array
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
#include "storage_manager.hpp"

// Initialize storage
StorageManager storage;

// Save configuration
uint8_t channel = 6;
std::vector<PersistentPeer> peers;
// ... populate peers
esp_err_t err = storage.save(channel, peers);

// Load configuration
uint8_t loaded_channel;
std::vector<PersistentPeer> loaded_peers;
err = storage.load(loaded_channel, loaded_peers);
```
### Optimized Save
```cpp
// Save with optimization (no NVS write if unchanged)
storage.save(channel, peers, false);
```

### Configuration Constants
```cpp
static constexpr uint32_t MAGIC   = 0x4553504E;
static constexpr uint32_t VERSION = 1;
static constexpr size_t MAX_PEERS = 19;
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
- Maximum of 19 persistent peers
- Data lost from RTC on deep sleep
- Requires NVS partition in partition table
- C++17 compatible compiler required

### See Also
[ESP-NOW Protocol Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html)

[ESP-IDF NVS Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html)