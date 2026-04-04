# ESP-NOW Manager — API Reference

Complete API documentation for the ESP-NOW Manager component. This reference covers all public methods, their parameters, return values, and usage notes.

---

## Table of Contents

- [Data Types and Enumerations](#data-types-and-enumerations)
  - [`EspNowConfig`](#espnowconfig)
  - [`NodeId`](#nodeid)
  - [`NodeType`](#nodetype)
  - [`NodeState`](#nodestate)
  - [`PeerInfo`](#peerinfo)
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
- [Pairing](#pairing)
  - [`start_pairing()`](#start_pairing)
- [Status](#status)
  - [`get_node_state()`](#get_node_state)
  - [`is_initialized()`](#is_initialized)

---

## Data Types and Enumerations

### `EspNowConfig`

Configuration structure for initializing the EspNowManager.

```cpp
struct EspNowConfig
{
    NodeId node_id;
    NodeType node_type;
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
| `node_id` | `NodeId` | `ReservedIds::HUB` | Logical ID for this device |
| `node_type` | `NodeType` | `ReservedTypes::UNKNOWN` | Role/Type for this device |
| `app_rx_queue` | `QueueHandle_t` | `nullptr` | **Required.** Handle to application queue where incoming DATA/COMMAND messages are posted |
| `wifi_channel` | `uint8_t` | `1` | Initial WiFi channel (1-14) |
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
EspNowConfig config;
config.node_id = ReservedIds::HUB;
config.node_type = ReservedTypes::HUB;
config.app_rx_queue = app_queue;  // Required!
config.wifi_channel = 6;
config.heartbeat_interval_ms = 30000;  // 30 second heartbeats

esp_err_t err = manager.init(config);
```

---

### `NodeId`

Unique logical identifier for a node in the ESP-NOW network.

```cpp
using NodeId = uint8_t;
```

**Reserved IDs:**

| ID | Value | Description |
|----|-------|-------------|
| `ReservedIds::HUB` | `0x01` | Central controller/coordinator |
| `ReservedIds::BROADCAST` | `0x00` | Broadcast address |

**Custom Node IDs:**

Application-specific node IDs can be defined using strongly-typed enums:

```cpp
// Define application-specific node IDs
enum class MyNodeId : uint8_t {
    SENSOR_1 = 0x10,
    SENSOR_2 = 0x11,
    SENSOR_3 = 0x12,
    ACTUATOR_1 = 0x20,
    ACTUATOR_2 = 0x21,
};

// Use with template overloads (automatic casting)
manager.send_data(MyNodeId::SENSOR_1, ...);

// Or cast explicitly
manager.send_data(static_cast<NodeId>(MyNodeId::SENSOR_1), ...);
```

**Best Practices:**
- Use values `0x02` to `0xFE` for custom node IDs
- Avoid `0x00` (broadcast) and `0x01` (reserved for HUB)
- Use enum classes for type safety and autocomplete

---

### `NodeType`

Categorization of a node's role in the network.

```cpp
using NodeType = uint8_t;
```

**Reserved Types:**

| Type | Value | Description |
|------|-------|-------------|
| `ReservedTypes::HUB` | `0x01` | Central controller |
| `ReservedTypes::UNKNOWN` | `0x00` | Unknown/unspecified type |

**Custom Node Types:**

```cpp
// Define application-specific node types
enum class MyNodeType : uint8_t {
    SENSOR = 0x02,
    ACTUATOR = 0x03,
    REPEATER = 0x04,
    GATEWAY = 0x05,
};

// Use with template overloads
manager.add_peer(node_id, mac, MyNodeType::SENSOR, heartbeat_ms);
```

---

### `NodeState`

Enumeration of node states managed by the internal state machine.

```cpp
enum class NodeState
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
NodeState state = manager.get_node_state();

switch (state) {
    case NodeState::OPERATIONAL:
        ESP_LOGI(TAG, "Ready to communicate");
        break;
    case NodeState::RECOVERY_SCAN:
        ESP_LOGW(TAG, "Connection lost, scanning...");
        break;
    case NodeState::IDLE:
        ESP_LOGW(TAG, "No peers, start pairing");
        manager.start_pairing(30000);
        break;
}
```

---

### `PeerInfo`

Detailed information about a registered peer.

```cpp
struct PeerInfo
{
    uint8_t mac[6];                 ///< 6-byte MAC address
    NodeType type;                  ///< Node type (HUB, peripheral, etc.)
    NodeId node_id;                 ///< Logical node ID
    uint64_t last_seen_ms;          ///< Last message timestamp (ms)
    bool paired;                    ///< Pairing completed
    uint32_t heartbeat_interval_ms; ///< Expected heartbeat interval
};
```

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `mac` | `uint8_t[6]` | Physical MAC address of the peer |
| `type` | `NodeType` | Role/type of the peer |
| `node_id` | `NodeId` | Logical identifier |
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
esp_err_t init(const EspNowConfig& config)
```

**Description:**  
Sets up the necessary resources, including WiFi, ESP-NOW drivers, tasks, and queues. For HUB: Prepares to receive data from multiple nodes and manage the peer list. For NODE: Prepares to communicate with the HUB and optionally starts heartbeats.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `config` | `const EspNowConfig&` | Configuration structure containing node ID, type, and resource settings |

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
EspNowConfig config;
config.node_id = ReservedIds::HUB;
config.node_type = ReservedTypes::HUB;
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

## Data Communication

### `send_data()`

Sends data payload to a destination node.

```cpp
esp_err_t send_data(
    NodeId dest_node_id,
    PayloadType payload_type,
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
| `dest_node_id` | `NodeId` | ID of the destination node |
| `payload_type` | `PayloadType` | Type identifier for the payload (application-defined) |
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
- Enters `NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures

**Warning:** Maximum payload is 230 bytes (ESP-NOW limit − header − CRC)

**Example:**
```cpp
SensorData data = {.temperature = 25.5, .humidity = 60};

esp_err_t err = manager.send_data(
    ReservedIds::HUB,              // Send to HUB
    PayloadType::SENSOR_DATA,      // Application-defined type
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
Allows using enum types directly for `NodeId` and `PayloadType`.

---

### `send_command()`

Sends a command to a destination node.

```cpp
esp_err_t send_command(
    NodeId dest_node_id,
    CommandType command_type,
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
| `dest_node_id` | `NodeId` | ID of the destination node |
| `command_type` | `CommandType` | Type of command to execute |
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
- Enters `NodeState::RECOVERY_SCAN` mode after `MAX_FAILURES` consecutive transmission failures

**Warning:** Maximum payload is 230 bytes

**Example:**
```cpp
SetIntervalCmd cmd = {.interval_ms = 5000};

esp_err_t err = manager.send_command(
    node_id,                       // Target node
    CommandType::SET_INTERVAL,     // Command type
    &cmd,                          // Command payload
    sizeof(cmd),
    true                           // Require ACK
);
```

**Template Overload (Enum Types):**
```cpp
template <typename T>
esp_err_t send_command(T dest_node_id, CommandType command_type, const void* payload, size_t len, bool require_ack = false)
```

---

### `confirm_reception()`

Confirms reception of a message that required an ACK.

```cpp
esp_err_t confirm_reception(NodeId sender_id, uint16_t sequence_number, AckStatus status)
```

**Description:**
Sends a logical acknowledgment back to the specified sender. This should be called by the application after processing a received message that had the `require_ack` flag set, to inform the sender of the processing outcome.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `sender_id` | `NodeId` | Logical ID of the sender node to acknowledge. |
| `sequence_number` | `uint16_t` | Sequence number of the original message being acknowledged. |
| `status` | `AckStatus` | Processing outcome: `AckStatus::OK` for success, `AckStatus::ERROR_INVALID_DATA` for invalid payload, or `AckStatus::ERROR_PROCESSING` for internal errors. |

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
    manager.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
}
```

---

## Peer Management

### `add_peer()`

Manually adds a peer to the manager.

```cpp
esp_err_t add_peer(
    NodeId node_id,
    const uint8_t* mac,
    NodeType type,
    uint32_t heartbeat_interval_ms
)
```

**Description:**  
Registers a node in the internal peer list and adds it to the ESP-NOW driver's peer table.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `node_id` | `NodeId` | Unique ID of the node |
| `mac` | `const uint8_t*` | MAC address of the node (6 bytes) |
| `type` | `NodeType` | Role/Type of the node |
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
    ReservedIds::HUB,      // HUB node ID
    hub_mac,               // HUB MAC address
    ReservedTypes::HUB,    // Node type
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
esp_err_t remove_peer(NodeId node_id)
```

**Description:**  
Removes the peer from both internal lists and the ESP-NOW driver.

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `node_id` | `NodeId` | ID of the node to remove |

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
etl::vector<PeerInfo, MAX_PEERS> get_peers()
```

**Description:**  
Returns a vector containing information for all registered peers.

**Returns:**
- Vector of `PeerInfo` structures
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
etl::vector<NodeId, MAX_PEERS> get_offline_peers() const
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
NodeState get_node_state() const
```

**Description:**  
Returns the current state of the node state machine.

**Returns:**
- Current `NodeState` value

**Notes:**
- This method does not return errors

**Example:**
```cpp
NodeState state = manager.get_node_state();
if (state == NodeState::OPERATIONAL) {
    ESP_LOGI(TAG, "Ready to communicate");
} else if (state == NodeState::RECOVERY_SCAN) {
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
