> **Important Note**: As of version 1.1.0, all public APIs and types have been moved into the `espnow` namespace to prevent naming collisions.

# ESP-NOW Manager — API Reference

Complete API documentation for the ESP-NOW Manager component. This reference covers all public methods, their parameters, return values, and usage notes.

---

## Table of Contents

- [Data Types and Enumerations](#data-types-and-enumerations)
  - [`espnow::EspNowConfig`](#espnowconfig)
  - [`espnow::NodeId`](#nodeid)
  - [`espnow::NodeType`](#nodetype)
  - [`espnow::NodeState`](#nodestate)
  - [`espnow::PeerInfo`](#peerinfo)
- [Lifecycle Methods](#lifecycle-methods)
  - [`init()`](#init)
  - [`deinit()`](#deinit)
- [Data Communication](#data-communication)
  - [`send_data()`](#send_data)
  - [`send_command()`](#send_command)
  - [`confirm_reception()`](#confirm_reception)
- [Peer Management](#peer-management)
  - [`add_peer()`](#add_peer)
  - [`remove_peer()`](#remove_peer)
  - [`get_peers()`](#get_peers)
  - [`get_offline_peers()`](#get_offline_peers)
  - [`get_peer_stats()`](#get_peer_stats)
  - [`get_all_peer_stats()`](#get_all_peer_stats)
- [Pairing](#pairing)
  - [`start_pairing()`](#start_pairing)
- [Status](#status)
  - [`get_node_state()`](#get_node_state)
  - [`is_initialized()`](#is_initialized)

---

## Data Types and Enumerations

### `espnow::EspNowConfig`

Configuration structure for initializing the EspNowManager.

```cpp
struct espnow::EspNowConfig
{
    espnow::NodeId node_id;
    espnow::NodeType node_type;
    QueueHandle_t app_rx_queue;
    uint8_t wifi_channel;
    uint32_t ack_timeout_ms;
    uint32_t heartbeat_interval_ms;
    uint32_t channel_monitor_interval_ms;
    uint32_t stack_size_rx_task;
    uint32_t stack_size_tx_task;
    uint32_t stack_size_discovery_task;
    UBaseType_t priority_rx_task;
    UBaseType_t priority_tx_task;
    UBaseType_t priority_discovery_task;
    uint32_t rx_queue_length;
    uint32_t tx_queue_length;
};
```

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `node_id` | `espnow::NodeId` | `espnow::ReservedIds::HUB` | Logical ID for this device |
| `node_type` | `espnow::NodeType` | `ReservedTypes::UNKNOWN` | Role/Type for this device |
| `app_rx_queue` | `QueueHandle_t` | `nullptr` | **Required.** Handle to application queue where incoming DATA/COMMAND messages are posted |
| `wifi_channel` | `uint8_t` | `1` | Initial starting point for discovery scan in `SCAN` mode. (Hardware WiFi channel setup is the application's responsibility). |
| `ack_timeout_ms` | `uint32_t` | `500` | Timeout for logical ACKs |
| `heartbeat_interval_ms` | `uint32_t` | `60000` | Heartbeat interval (0 disables) |
| `channel_monitor_interval_ms` | `uint32_t` | `10000` | Channel monitoring interval |
| `stack_size_rx_task` | `uint32_t` | `4096` | RX task stack size |
| `stack_size_tx_task` | `uint32_t` | `4096` | TX task stack size |
| `stack_size_discovery_task` | `uint32_t` | `4096` | Discovery task stack size |
| `priority_rx_task` | `UBaseType_t` | `10` | RX task priority |
| `priority_tx_task` | `UBaseType_t` | `9` | TX task priority |
| `priority_discovery_task` | `UBaseType_t` | `5` | Discovery task priority |
| `rx_queue_length` | `uint32_t` | `30` | RX queue length |
| `tx_queue_length` | `uint32_t` | `20` | TX queue length |

**Example:**
```cpp
// Create application queue
QueueHandle_t app_queue = xQueueCreate(30, sizeof(AppMessage));

// Configure with defaults, override what's needed
espnow::EspNowConfig config;
config.node_id = espnow::ReservedIds::HUB;
config.node_type = espnow::ReservedTypes::HUB;
config.app_rx_queue = app_queue;  // Required!
config.wifi_channel = 6;
config.heartbeat_interval_ms = 30000;  // 30 second heartbeats

esp_err_t err = manager.init(config);
```

---

### `espnow::NodeId`

Unique logical identifier for a node in the ESP-NOW network.

```cpp
using espnow::NodeId = uint8_t;
```

**Reserved IDs:**

| ID | Value | Description |
|----|-------|-------------|
| `espnow::ReservedIds::HUB` | `0x01` | Central controller/coordinator |
| `ReservedIds::BROADCAST` | `0x00` | Broadcast address |

**Custom Node IDs:**

Application-specific node IDs can be defined using strongly-typed enums:

```cpp
// Define application-specific node IDs
enum class Myespnow::NodeId : uint8_t {
    SENSOR_1 = 0x10,
    SENSOR_2 = 0x11,
    SENSOR_3 = 0x12,
    ACTUATOR_1 = 0x20,
    ACTUATOR_2 = 0x21,
};

// Use with template overloads (automatic casting)
manager.send_data(Myespnow::NodeId::SENSOR_1, ...);

// Or cast explicitly
manager.send_data(static_cast<espnow::NodeId>(Myespnow::NodeId::SENSOR_1), ...);
```

**Best Practices:**
- Use values `0x02` to `0xFE` for custom node IDs
- Avoid `0x00` (broadcast) and `0x01` (reserved for HUB)
- Use enum classes for type safety and autocomplete

---

### `espnow::NodeType`

Categorization of a node's role in the network.

```cpp
using espnow::NodeType = uint8_t;
```

**Reserved Types:**

| Type | Value | Description |
|------|-------|-------------|
| `espnow::ReservedTypes::HUB` | `0x01` | Central controller |
| `ReservedTypes::UNKNOWN` | `0x00` | Unknown/unspecified type |

**Custom Node Types:**

```cpp
// Define application-specific node types
enum class Myespnow::NodeType : uint8_t {
    SENSOR = 0x02,
    ACTUATOR = 0x03,
    REPEATER = 0x04,
    GATEWAY = 0x05,
};

// Use with template overloads
manager.add_peer(node_id, mac, Myespnow::NodeType::SENSOR, heartbeat_ms);
```

---

### `espnow::NodeState`

Enumeration of node states managed by the internal state machine.

```cpp
enum class espnow::NodeState
{
    UNINITIALIZED = 0,  ///< Before init()
    IDLE = 1,           ///< Initialized, no peers
    PAIRING = 2,        ///< Actively pairing
    OPERATIONAL = 3,    ///< Has peers, normal operation
    PAIRING_SCAN = 4,   ///< Scanning for HUB to pair
    RECOVERY_SCAN = 5,  ///< Lost connection, scanning
    COUNT = 6           ///< Number of states (for validation)
};
```

**State Transitions:**

```
UNINITIALIZED ──init()──> IDLE ──start_pairing()──> PAIRING_SCAN
                              │                          │
                              │ has peers                │ channel found
                              ▼                          ▼
                        OPERATIONAL <────────────── PAIRING
                              │                          │
                              │ link lost                │ pairing timeout
                              ▼                          ▼
                        RECOVERY_SCAN ───────────> IDLE / OPERATIONAL
```

**Typical Flow:**
1. **UNINITIALIZED** → Device powers on
2. **IDLE** or **OPERATIONAL** → After `init()` (depends on stored peers)
3. **PAIRING_SCAN** or **PAIRING** → During `start_pairing()`
4. **OPERATIONAL** → Pairing complete, ready for communication
5. **RECOVERY_SCAN** → Connection lost, searching for channel
6. **IDLE** or **OPERATIONAL** → After scan completes

**Example:**
```cpp
espnow::NodeState state = manager.get_node_state();

switch (state) {
    case espnow::NodeState::OPERATIONAL:
        ESP_LOGI(TAG, "Ready to communicate");
        break;
    case espnow::NodeState::RECOVERY_SCAN:
        ESP_LOGW(TAG, "Connection lost, scanning...");
        break;
    case espnow::NodeState::IDLE:
        ESP_LOGW(TAG, "No peers, start pairing");
        manager.start_pairing(30000);
        break;
}
```

---

### `espnow::ChannelPolicy`

Controls whether the `DiscoveryManager` is allowed to change the WiFi channel during discovery scanning.

```cpp
enum class espnow::ChannelPolicy : uint8_t
{
    SCAN,   ///< Dynamic scanning mode: discovery scan iterates through channels using wifi_set_channel()
    FIXED,  ///< Fixed channel mode: node is connected to a WiFi AP; channel is owned by AP and scanning is disabled
};
```

**Enum Values:**

| Value | Description | Use Case |
|-------|-------------|----------|
| `SCAN` | Node iterates through channels 1-13 during discovery scans. | Standalone ESP-NOW (no AP connection) |
| `FIXED` | Discovery scan skips channel switching and assumes shared AP channel. | Simultaneous WiFi STA + ESP-NOW operation |

---

### `espnow::PeerInfo`

Detailed information about a registered peer.

```cpp
struct espnow::PeerInfo
{
    uint8_t mac[6];                 ///< 6-byte MAC address
    espnow::NodeType type;                  ///< Node type (HUB, peripheral, etc.)
    espnow::NodeId node_id;                 ///< Logical node ID
    uint64_t last_seen_ms;          ///< Last message timestamp (ms)
    bool paired;                    ///< Pairing completed
    uint32_t heartbeat_interval_ms; ///< Expected heartbeat interval
};
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `mac` | `uint8_t[6]` | Physical MAC address of the peer |
| `type` | `espnow::NodeType` | Role/type of the peer |
| `node_id` | `espnow::NodeId` | Logical identifier |
| `last_seen_ms` | `uint64_t` | Timestamp of last received message |
| `paired` | `bool` | `true` if pairing completed successfully |
| `heartbeat_interval_ms` | `uint32_t` | Expected interval between heartbeats |

**Usage with `get_peers()`:**

```cpp
// Get all registered peers
auto peers = manager.get_peers();

// Iterate through peers
for (const auto& peer : peers) {
    ESP_LOGI(TAG, "Peer ID: %d, Type: %d", peer.node_id, peer.type);
    ESP_LOGI(TAG, "  MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             peer.mac[0], peer.mac[1], peer.mac[2],
             peer.mac[3], peer.mac[4], peer.mac[5]);
    ESP_LOGI(TAG, "  Last seen: %lu ms ago", 
             (uint64_t)(esp_timer_get_time() / 1000 - peer.last_seen_ms));
    ESP_LOGI(TAG, "  Paired: %s", peer.paired ? "yes" : "no");
}

// Check if specific peer exists
bool found = false;
for (const auto& peer : peers) {
    if (peer.node_id == target_id) {
        found = true;
        break;
    }
}
```

**Usage with `get_offline_peers()`:**

```cpp
// Get peers that haven't sent heartbeat recently
auto offline = manager.get_offline_peers();

if (!offline.empty()) {
    ESP_LOGW(TAG, "%d peers offline:", offline.size());
    for (const auto& node_id : offline) {
        ESP_LOGW(TAG, "  - Node %d", node_id);
    }
}
```

---

## Lifecycle Methods

### `init()`

Initializes the ESP-NOW Manager with the specified configuration.

```cpp
esp_err_t init(const espnow::EspNowConfig& config)
```

**Description:**  
Sets up the necessary resources, including WiFi, ESP-NOW drivers, tasks, and queues. For HUB: Prepares to receive data from multiple nodes and manage the peer list. For NODE: Prepares to communicate with the HUB and optionally starts heartbeats.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `config` | `const espnow::EspNowConfig&` | Configuration structure containing node ID, type, and resource settings |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Success |
| `ESP_ERR_INVALID_STATE` | Already initialized or `wifi_set_mode() == WIFI_MODE_NULL` |
| `ESP_ERR_INVALID_ARG` | `app_rx_queue` is null |
| `ESP_ERR_NO_MEM` | Memory allocation for mutex and internal queues failed |
| `ESP_FAIL` | Failed to create internal tasks |
| Other | Internal `esp_now.h` or `esp_wifi.h` errors |

**Notes:**
- This method must be called before any other operation
- On failure, `deinit()` is called automatically to clean up resources

**Example:**
```cpp
espnow::EspNowConfig config;
config.node_id = espnow::ReservedIds::HUB;
config.node_type = espnow::ReservedTypes::HUB;
config.app_rx_queue = app_queue;
config.wifi_channel = 6;

esp_err_t err = manager.init(config);
if (err != ESP_OK) {
    ESP_LOGE(TAG, "Init failed: %s", esp_err_to_name(err));
}
```

---

### `deinit()`

Deinitializes the ESP-NOW Manager and releases all resources.

```cpp
void deinit()
```

**Description:**  
Stops all background tasks, releases memory, and deinitializes the ESP-NOW driver.

**Notes:**
- Idempotent — safe to call multiple times
- This method does not return errors

**Example:**
```cpp
manager.deinit();
// Safe to call again
manager.deinit();
```

---

### `set_channel_policy()`

Sets the channel policy for discovery scanning.

```cpp
void set_channel_policy(espnow::ChannelPolicy policy)
```

**Description:**  
Informs `EspNowManager` whether it is permitted to change WiFi channels when scanning for a HUB.

- Use `ChannelPolicy::FIXED` when the device is connected to a WiFi Access Point (STA mode). When connected, channel ownership belongs to the WiFi connection and attempts to call `esp_wifi_set_channel()` will fail or disrupt the AP link.
- Use `ChannelPolicy::SCAN` (default) when operating in standalone ESP-NOW mode without an active WiFi AP connection.

**Example:**
```cpp
// When connected to a WiFi AP:
manager.set_channel_policy(espnow::ChannelPolicy::FIXED);

// If WiFi disconnects:
manager.set_channel_policy(espnow::ChannelPolicy::SCAN);
```

---

## Data Communication

### `send_data()`

Sends data payload to a destination node.

```cpp
esp_err_t send_data(
    espnow::NodeId dest_node_id,
    espnow::PayloadType payload_type,
    const void* payload,
    size_t len,
    bool require_ack = false
)
```

**Description:**  
Encapsulates the payload into a standard message format and queues it for transmission. For HUB: Used to send application data to a specific registered node. For NODE: Typically used to send sensor data or status updates to the HUB.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `dest_node_id` | `espnow::NodeId` | ID of the destination node |
| `payload_type` | `espnow::PayloadType` | Type identifier for the payload (application-defined) |
| `payload` | `const void*` | Pointer to the data buffer to be sent |
| `len` | `size_t` | Length of the payload in bytes |
| `require_ack` | `bool` | If `true`, waits for a logical acknowledgment (default: `false`) |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Packet successfully queued |
| `ESP_ERR_INVALID_STATE` | Manager not in `OPERATIONAL` state or `tx_queue` not initialized |
| `ESP_ERR_NOT_FOUND` | Peer is not registered |
| `ESP_ERR_INVALID_ARG` | Payload length exceeds `MAX_PAYLOAD_SIZE` (230 bytes) |
| `ESP_FAIL` | Failed to send message to `tx_queue_` |

**Notes:**
- Non-blocking unless `require_ack=true`
- Enters `espnow::NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures

**Warning:** Maximum payload is 230 bytes (ESP-NOW limit − header − CRC)

**Example:**
```cpp
SensorData data = {.temperature = 25.5, .humidity = 60};

esp_err_t err = manager.send_data(
    espnow::ReservedIds::HUB,              // Send to HUB
    espnow::PayloadType::SENSOR_DATA,      // Application-defined type
    &data,                         // Payload pointer
    sizeof(data),                  // Payload size
    true                           // Require ACK
);

if (err == ESP_OK) {
    ESP_LOGI(TAG, "Data sent");
} else if (err == ESP_ERR_NOT_FOUND) {
    ESP_LOGE(TAG, "HUB not found");
}
```

**Template Overload (Enum Types):**
```cpp
template <typename T1, typename T2>
esp_err_t send_data(T1 dest_node_id, T2 payload_type, const void* payload, size_t len, bool require_ack = false)
```
Allows using enum types directly for `espnow::NodeId` and `espnow::PayloadType`.

---

### `send_command()`

Sends a command to a destination node.

```cpp
esp_err_t send_command(
    espnow::NodeId dest_node_id,
    espnow::CommandType command_type,
    const void* payload,
    size_t len,
    bool require_ack = false
)
```

**Description:**  
Similar to `send_data()`, but specifically for control commands. For HUB: Used to control node behavior (e.g., change reporting interval). For NODE: Can be used to request actions from the HUB.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `dest_node_id` | `espnow::NodeId` | ID of the destination node |
| `command_type` | `espnow::CommandType` | Type of command to execute |
| `payload` | `const void*` | Optional payload for the command |
| `len` | `size_t` | Length of the payload |
| `require_ack` | `bool` | If `true`, waits for logical acknowledgment (default: `false`) |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Success |
| `ESP_ERR_INVALID_STATE` | Manager not in `OPERATIONAL` state or `tx_queue` not initialized |
| `ESP_ERR_NOT_FOUND` | Peer is not registered |
| `ESP_ERR_INVALID_ARG` | Payload length exceeds `MAX_PAYLOAD_SIZE` |
| `ESP_FAIL` | Failed to send message to `tx_queue_` |

**Notes:**
- Non-blocking unless `require_ack=true`
- Enters `espnow::NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures

**Warning:** Maximum payload is 230 bytes

**Example:**
```cpp
SetIntervalCmd cmd = {.interval_ms = 5000};

esp_err_t err = manager.send_command(
    node_id,                       // Target node
    espnow::espnow::CommandType::SET_INTERVAL,     // Command type
    &cmd,                          // Command payload
    sizeof(cmd),
    true                           // Require ACK
);
```

**Template Overload (Enum Types):**
```cpp
template <typename T>
esp_err_t send_command(T dest_node_id, espnow::CommandType command_type, const void* payload, size_t len, bool require_ack = false)
```

---

### `confirm_reception()`

Confirms reception of a message that required an ACK.

```cpp
esp_err_t confirm_reception(espnow::NodeId sender_id, uint16_t sequence_number, espnow::AckStatus status)
```

**Description:**
Sends a logical acknowledgment back to the specified sender. This should be called by the application after processing a received message that had the `require_ack` flag set, to inform the sender of the processing outcome.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `sender_id` | `espnow::NodeId` | Logical ID of the sender node to acknowledge. |
| `sequence_number` | `uint16_t` | Sequence number of the original message being acknowledged. |
| `status` | `espnow::AckStatus` | Processing outcome: `espnow::AckStatus::OK` for success, `espnow::AckStatus::ERROR_INVALID_DATA` for invalid payload, or `espnow::AckStatus::ERROR_PROCESSING` for internal errors. |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | ACK was queued successfully |
| `ESP_ERR_INVALID_STATE` | Manager not in `OPERATIONAL`/`PAIRING` state, or `tx_queue` not initialized |
| `ESP_ERR_NOT_FOUND` | Peer MAC not found for the specified sender_id |
| `ESP_FAIL` | Failed to queue ACK packet for transmission |

**Example:**
```cpp
// In your RX task, after processing a received message:
if (msg.requires_ack) {
    manager.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
}
```

---

## Peer Management

### `add_peer()`

Manually adds a peer to the manager.

```cpp
esp_err_t add_peer(
    espnow::NodeId node_id,
    const uint8_t* mac,
    espnow::NodeType type,
    uint32_t heartbeat_interval_ms
)
```

**Description:**  
Registers a node in the internal peer list and adds it to the ESP-NOW driver's peer table.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `node_id` | `espnow::NodeId` | Unique ID of the node |
| `mac` | `const uint8_t*` | MAC address of the node (6 bytes) |
| `type` | `espnow::NodeType` | Role/Type of the node |
| `heartbeat_interval_ms` | `uint32_t` | Heartbeat interval in milliseconds |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Success |
| `ESP_ERR_INVALID_ARG` | `mac` is `nullptr` |
| `ESP_ERR_TIMEOUT` | Failed to acquire mutex within timeout |
| `ESP_ERR_NO_MEM` | ESP-NOW driver failed to allocate memory for peer |
| `ESP_ERR_ESPNOW_NOT_INIT` | ESP-NOW driver not initialized |
| `ESP_ERR_ESPNOW_ARG` | Invalid argument passed to ESP-NOW driver |
| `ESP_ERR_ESPNOW_NO_MEM` | ESP-NOW driver out of memory |
| `ESP_ERR_ESPNOW_NOT_FOUND` | Peer not found when updating existing peer |
| `ESP_ERR_ESPNOW_CHAN` | Invalid WiFi channel |
| `ESP_ERR_ESPNOW_IF` | Invalid interface |
| `ESP_ERR_WIFI_NOT_INIT` | WiFi not initialized |
| `ESP_ERR_WIFI_NOT_STARTED` | WiFi not started |
| `ESP_ERR_WIFI_ARG` | Invalid WiFi argument |
| `ESP_ERR_INVALID_STATE` | Storage failed to persist peer data |

**Notes:**
- List uses LRU (Least Recently Used) policy with maximum `MAX_PEERS = 19` (ESP-NOW limitation)
- When full, oldest peer (least recently seen) is removed to make room
- Re-adding existing peer moves it to front (marks as recently used)
- Automatically persisted to RTC and NVS storage if it's a new peer or re-adding one with different MAC.

**Warning:** ESP-NOW hardware limit is 20 peers, but 1 is reserved for broadcast

**Example:**
```cpp
uint8_t hub_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

esp_err_t err = manager.add_peer(
    espnow::ReservedIds::HUB,      // HUB node ID
    hub_mac,               // HUB MAC address
    espnow::ReservedTypes::HUB,    // Node type
    60000                  // 1 minute heartbeat
);
```

**Template Overload (Enum Types):**
```cpp
template <typename T1, typename T2>
esp_err_t add_peer(T1 node_id, const uint8_t* mac, T2 type, uint32_t heartbeat_interval_ms)
```

---

### `remove_peer()`

Removes a peer from the manager.

```cpp
esp_err_t remove_peer(espnow::NodeId node_id)
```

**Description:**  
Removes the peer from both internal lists and the ESP-NOW driver.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `node_id` | `espnow::NodeId` | ID of the node to remove |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Success |
| `ESP_ERR_NOT_FOUND` | Peer is not present |
| `ESP_ERR_TIMEOUT` | Failed to acquire mutex within timeout |
| `ESP_ERR_ESPNOW_NOT_INIT` | ESP-NOW driver not initialized |
| `ESP_ERR_ESPNOW_ARG` | Invalid argument passed to ESP-NOW driver |
| `ESP_ERR_ESPNOW_NOT_FOUND` | Peer not found in ESP-NOW driver |
| `ESP_ERR_ESPNOW_CHAN` | Invalid WiFi channel |
| `ESP_ERR_ESPNOW_IF` | Invalid interface |
| `ESP_ERR_WIFI_NOT_INIT` | WiFi not initialized |
| `ESP_ERR_WIFI_NOT_STARTED` | WiFi not started |
| `ESP_ERR_WIFI_ARG` | Invalid WiFi argument |
| `ESP_ERR_INVALID_STATE` | Storage failed to persist peer removal |

**Example:**
```cpp
esp_err_t err = manager.remove_peer(node_id);
if (err == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Peer not found");
}
```

**Template Overload (Enum Types):**
```cpp
template <typename T>
esp_err_t remove_peer(T node_id)
```

---

### `get_peers()`

Gets a list of all registered peers.

```cpp
etl::vector<espnow::PeerInfo, MAX_PEERS> get_peers()
```

**Description:**  
Returns a vector containing information for all registered peers.

**Returns:**
- Vector of `espnow::PeerInfo` structures
- Returns empty vector if mutex acquisition fails

**Notes:**
- This method does not return errors

**Example:**
```cpp
auto peers = manager.get_peers();
for (const auto& peer : peers) {
    ESP_LOGI(TAG, "Peer ID: %d, Type: %d", peer.node_id, peer.type);
}
```

---

### `get_offline_peers()`

Gets a list of IDs for peers considered offline.

```cpp
etl::vector<espnow::NodeId, MAX_PEERS> get_offline_peers() const
```

**Description:**  
A peer is considered offline if no heartbeat has been received within its expected interval multiplied by `HEARTBEAT_OFFLINE_MULTIPLIER`.

**Returns:**
- Vector of Node IDs
- Returns empty vector if mutex acquisition fails or manager not operational

**Notes:**
- This method does not return errors
- See `HEARTBEAT_OFFLINE_MULTIPLIER` in `protocol_types.hpp`

**Example:**
```cpp
auto offline = manager.get_offline_peers();
if (!offline.empty()) {
    ESP_LOGW(TAG, "%d peers offline", offline.size());
}
```

---

## Statistics

### `get_peer_stats()`

Gets statistics for a specific peer.

```cpp
bool get_peer_stats(espnow::NodeId node_id, espnow::PeerStatistics& out) const
```

**Description:**
Retrieves current link quality metrics for a specific peer identified by `node_id`. Statistics include RSSI, RTT, packet counters, delivery failures, driver errors, and packet loss.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `node_id` | `espnow::NodeId` | Logical ID of the peer |
| `out` | `espnow::PeerStatistics&` | Output parameter filled with current statistics |

**Returns:**
- `true` if the peer was found and `out` was populated
- `false` if the peer is not tracked or statistics are not yet available

**Example:**
```cpp
espnow::PeerStatistics stats;
if (manager.get_peer_stats(espnow::ReservedIds::HUB, stats)) {
    ESP_LOGI(TAG, "HUB RSSI: %d dBm (avg), RTT: %lu ms",
             stats.rssi_avg, (unsigned long)stats.rtt_avg_ms);
    ESP_LOGI(TAG, "Packets: rx=%lu, tx=%lu, lost=%lu",
             (unsigned long)stats.packets_rx,
             (unsigned long)stats.packets_sent,
             (unsigned long)stats.packets_lost);
} else {
    ESP_LOGW(TAG, "No statistics available for HUB");
}
```

---

### `get_all_peer_stats()`

Gets statistics for all tracked peers.

```cpp
etl::vector<espnow::PeerStatistics, MAX_PEERS> get_all_peer_stats() const
```

**Description:**
Returns a vector containing link quality statistics for all peers currently tracked by the statistics manager. Useful for dashboards or periodic health reports.

**Returns:**
- Vector of `espnow::PeerStatistics` for all tracked peers
- Empty vector if no peers are tracked

**Notes:**
- This method does not return errors
- Thread-safe: uses `portMAX_DELAY` mutex timeout for app thread callers

**Example:**
```cpp
auto all_stats = manager.get_all_peer_stats();

ESP_LOGI(TAG, "Tracking %d peers:", all_stats.size());
for (const auto& stats : all_stats) {
    ESP_LOGI(TAG, "  Node %d: RSSI=%d dBm, RTT=%lu ms, "
             "tx=%lu, rx=%lu, lost=%lu",
             stats.node_id,
             stats.rssi_avg,
             (unsigned long)stats.rtt_avg_ms,
             (unsigned long)stats.packets_sent,
             (unsigned long)stats.packets_rx,
             (unsigned long)stats.packets_lost);
}
```

---

### `espnow::PeerStatistics` Structure

Detailed link quality metrics for a single peer.

```cpp
struct espnow::PeerStatistics
{
    espnow::NodeId node_id;                   ///< Logical node ID
    int8_t rssi_last;                 ///< Last received RSSI (dBm)
    int8_t rssi_avg;                  ///< Exponential moving average of RSSI (-127 = unknown)
    uint8_t rssi_alpha;               ///< EMA alpha weight (derived from heartbeat interval)
    uint32_t packets_rx;              ///< Total packets received
    uint32_t packets_sent;            ///< Successful transmissions (callback ESP_NOW_SEND_SUCCESS)
    uint32_t delivery_failures;       ///< MAC/PHY failures (callback ESP_NOW_SEND_FAIL)
    uint32_t driver_errors;           ///< hal_esp_now_send() returned error
    uint32_t packets_lost;            ///< ACK timeouts after retries exhausted
    uint32_t retries;                 ///< Number of retransmissions
    uint32_t rtt_last_ms;             ///< Last round-trip time (ms)
    uint32_t rtt_avg_ms;              ///< Exponential moving average of RTT (0 = unknown)
};
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `node_id` | `espnow::NodeId` | Logical identifier of the peer |
| `rssi_last` | `int8_t` | Most recent RSSI value in dBm |
| `rssi_avg` | `int8_t` | EMA-smoothed RSSI average. `-127` (`RSSI_UNKNOWN`) means no data yet |
| `rssi_alpha` | `uint8_t` | Fixed-point EMA alpha (0-256, where 256 = 100% new sample) |
| `packets_rx` | `uint32_t` | Total valid packets received from this peer |
| `packets_sent` | `uint32_t` | Successful over-the-air transmissions |
| `delivery_failures` | `uint32_t` | MAC/PHY layer delivery failures |
| `driver_errors` | `uint32_t` | ESP-NOW driver send errors (NO_MEM, CHAN, etc.) |
| `packets_lost` | `uint32_t` | Packets lost due to ACK timeout after all retries |
| `retries` | `uint32_t` | Total retransmission attempts |
| `rtt_last_ms` | `uint32_t` | Most recent round-trip time in milliseconds |
| `rtt_avg_ms` | `uint32_t` | EMA-smoothed RTT average. `0` means no data yet |

**EMA Details:**
- RSSI alpha is derived from heartbeat interval: shorter intervals → smoother (less reactive)
- RTT uses a fixed alpha of 32/256 (12.5%) for stability
- EMA formula: `avg = (sample * alpha + avg * (256 - alpha)) >> 8`

---

## Pairing

### `start_pairing()`

Starts the pairing process.

```cpp
esp_err_t start_pairing(uint32_t timeout_ms = 30000)
```

**Description:**

**For HUB:**
- Enters listening mode for pairing requests
- Accepts requests from any node broadcasting
- Automatically adds responding peers to peer list

**For NODE:**
- Broadcasts pairing request periodically (every 1s)
- Waits for HUB to respond with acknowledgment
- Automatically stops when paired or timeout reached

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `timeout_ms` | `uint32_t` | Duration of the pairing mode in milliseconds (default: 30000) |

**Returns:**
| Error Code | Description |
|------------|-------------|
| `ESP_OK` | Pairing started successfully |
| `ESP_ERR_INVALID_STATE` | Manager is `UNINITIALIZED` or pairing already active |

**Notes:**
- Automatic stop after specified timeout duration

**Warning:** Both HUB and NODE must be in pairing mode simultaneously

**Example:**
```cpp
// As HUB - accept new nodes for 60 seconds
esp_err_t err = manager.start_pairing(60000);

// As NODE - seek a HUB for 30 seconds
err = manager.start_pairing(30000);
```

---

## Status

### `get_node_state()`

Gets the current node state.

```cpp
espnow::NodeState get_node_state() const
```

**Description:**  
Returns the current state of the node state machine.

**Returns:**
- Current `espnow::NodeState` value

**Notes:**
- This method does not return errors

**Example:**
```cpp
espnow::NodeState state = manager.get_node_state();
if (state == espnow::NodeState::OPERATIONAL) {
    ESP_LOGI(TAG, "Ready to communicate");
} else if (state == espnow::NodeState::RECOVERY_SCAN) {
    ESP_LOGW(TAG, "Scanning for channel...");
}
```

---

### `is_initialized()`

Checks if EspNowManager is initialized.

```cpp
bool is_initialized() const
```

**Description:**  
Returns `true` if the manager has been successfully initialized.

**Returns:**
- `true` if initialized
- `false` otherwise

**Notes:**
- This method does not return errors

**Example:**
```cpp
if (!manager.is_initialized()) {
    ESP_LOGE(TAG, "Manager not initialized");
    return;
}
```

---

## Related Documentation

- [`README.md`](README.md) — Overview and usage guide
- [`DESIGN.md`](DESIGN.md) — Internal architecture and design decisions
- [`espnow_types.hpp`](include/espnow_types.hpp) — Type definitions and constants

---

*Generated from Doxygen comments in source code.*
