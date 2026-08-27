> **Note**: This document is automatically generated from Doxygen comments in source headers.

# API Reference

---

## Table of Contents

- [Data Types and Structures](#data-types-and-structures)
  - [`espnow::AppMessage`](#appmessage)
  - [`espnow::EspNowConfig`](#espnowconfig)
  - [`espnow::PeerInfo`](#peerinfo)
  - [`espnow::PeerStatistics`](#peerstatistics)
  - [`espnow::PeerStatisticsPersist`](#peerstatisticspersist)
  - [`espnow::PersistentPeer`](#persistentpeer)
  - [`AckStatus`](#ackstatus)
  - [`NodeState`](#nodestate)
  - [`ChannelPolicy`](#channelpolicy)
- [Public API (espnow::IEspNowManager)](#public-api-iespnowmanager)
  - [`init()`](#init)
  - [`deinit()`](#deinit)
  - [`set_channel_policy()`](#set_channel_policy)
  - [`send_data()`](#send_data)
  - [`send_data()`](#send_data)
  - [`send_command()`](#send_command)
  - [`send_command()`](#send_command)
  - [`confirm_reception()`](#confirm_reception)
  - [`add_peer()`](#add_peer)
  - [`add_peer()`](#add_peer)
  - [`remove_peer()`](#remove_peer)
  - [`remove_peer()`](#remove_peer)
  - [`get_peer()`](#get_peer)
  - [`get_peer()`](#get_peer)
  - [`has_peer()`](#has_peer)
  - [`has_peer()`](#has_peer)
  - [`get_peer_count()`](#get_peer_count)
  - [`get_peers()`](#get_peers)
  - [`get_peer_stats()`](#get_peer_stats)
  - [`get_peer_stats()`](#get_peer_stats)
  - [`get_all_peer_stats()`](#get_all_peer_stats)
  - [`get_offline_peers()`](#get_offline_peers)
  - [`is_peer_online()`](#is_peer_online)
  - [`is_peer_online()`](#is_peer_online)
  - [`start_pairing()`](#start_pairing)
  - [`reconnect()`](#reconnect)
  - [`set_enable_heartbeat()`](#set_enable_heartbeat)
  - [`is_heartbeat_enabled()`](#is_heartbeat_enabled)
  - [`set_heartbeat_interval_ms()`](#set_heartbeat_interval_ms)
  - [`get_node_state()`](#get_node_state)
  - [`is_initialized()`](#is_initialized)

---

## Data Types and Structures

### `espnow::AppMessage`

Message delivered to the application layer after protocol processing.

This struct decouples the application from the internal protocol details. The rx_dispatch_task extracts the relevant fields from the decoded packet and posts this to the app_rx_queue — the application never needs to know about MessageHeader, RxPacket, or any ESP-NOW internals.

| Field | Type | Description |
| :--- | :--- | :--- |
| `sender_id` | `NodeId` | Logical ID of the sending node. |
| `sender_type` | `NodeType` | Role/type of the sending node. |
| `msg_type` | `MessageType` | Type of the message, DATA, COMMAND... |
| `payload_type` | `PayloadType` | Application-defined payload identifier. |
| `sequence_number` | `uint16_t` | Sequence number for ACK validation. |
| `requires_ack` | `bool` | If true, call confirm_reception() after processing. |
| `src_mac` | `uint8_t` | MAC address of the sender. |
| `rssi` | `int8_t` | RSSI of the received signal (dBm) |
| `payload` | `uint8_t` | Raw payload bytes (cast to your message struct) |
| `payload_len` | `size_t` | Number of valid bytes in payload[]. |

**Example:**
```cpp
espnow::AppMessage msg;
if (xQueueReceive(app_queue, &msg, portMAX_DELAY) == pdTRUE) {
    if (msg.payload_type == farm::PayloadType::WATER_LEVEL_REPORT) {
        auto *report = reinterpret_cast<const WaterLevelReport *>(msg.payload);
        process_report(report);
    }
    if (msg.requires_ack) {
        manager.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
    }
}
```

### `espnow::EspNowConfig`

Configuration structure for initializing the EspNowManager.

| Field | Type | Description |
| :--- | :--- | :--- |
| `node_id` | `NodeId` | Logical ID for this device. |
| `node_type` | `NodeType` | Role/Type for this device. |
| `app_rx_queue` | `QueueHandle_t` | Handle to the application queue where incoming DATA/COMMANDS are posted. |
| `wifi_channel` | `uint8_t` | Initial WiFi channel to operate on. |
| `ack_timeout_ms` | `uint32_t` | Timeout for logical acknowledgments (ms) |
| `heartbeat_interval_ms` | `uint32_t` | Contractual maximum reporting interval (ms) communicated to the Hub during pairing. |
| `enable_heartbeat` | `bool` | Controls autonomous HEARTBEAT packet emission. |
| `channel_monitor_interval_ms` | `uint32_t` | Interval for channel monitoring (ms) |
| `scan_max_backoff_ms` | `uint32_t` | Maximum backoff interval between recovery scan attempts (ms). |
| `logical_ack_retries` | `uint8_t` | Maximum retries for logical ACK timeout. |
| `stack_size_rx_task` | `uint32_t` | Stack size for the internal packet dispatcher task. |
| `stack_size_tx_task` | `uint32_t` | Stack size for the transmission manager task. |
| `stack_size_discovery_task` | `uint32_t` | Stack size for the discovery task. |
| `priority_rx_task` | `UBaseType_t` | Priority for the internal packet dispatcher task. |
| `priority_tx_task` | `UBaseType_t` | Priority for the transmission manager task. |
| `priority_discovery_task` | `UBaseType_t` | Priority for the discovery task. |
| `rx_queue_length` | `uint32_t` | Length of the internal packet dispatcher queue. |
| `tx_queue_length` | `uint32_t` | Length of the internal packet dispatcher queue. |

**Example:**
```cpp
// Create application queue
QueueHandle_t app_queue = xQueueCreate(30, sizeof(espnow::AppMessage));

// Configure with defaults, override what's needed
espnow::EspNowConfig config;
config.node_id = espnow::ReservedIds::HUB;
config.node_type = espnow::ReservedTypes::HUB;
config.app_rx_queue = app_queue; // Required!
config.wifi_channel = 6;

esp_err_t err = manager.init(config);
```

### `espnow::PeerInfo`

Detailed information about a registered peer.

| Field | Type | Description |
| :--- | :--- | :--- |
| `mac` | `uint8_t` | 6-byte MAC address of the peer |
| `type` | `NodeType` | Categorization of the node (e.g., HUB or peripheral) |
| `node_id` | `NodeId` | Unique logical ID assigned to the node. |
| `last_seen_ms` | `int64_t` | Timestamp of the last message received (ms) |
| `paired` | `bool` | If true, the node has completed the pairing process. |
| `heartbeat_interval_ms` | `uint32_t` | Expected frequency of heartbeat messages. |

**Example:**
```cpp
espnow::PeerInfo peer{};
if (manager.get_peer(farm::NodeId::PUMP_CONTROL, peer)) {
    ESP_LOGI(TAG, "Peer Node: 0x%02X, Type: 0x%02X", peer.node_id, peer.type);
    ESP_LOGI(TAG, "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             peer.mac[0], peer.mac[1], peer.mac[2],
             peer.mac[3], peer.mac[4], peer.mac[5]);
}
```

### `espnow::PeerStatistics`

Structure for peer statistics.

| Field | Type | Description |
| :--- | :--- | :--- |
| `node_id` | `NodeId` | Node ID. |
| `rssi_last` | `int8_t` | Last received RSSI. |
| `rssi_alpha` | `uint8_t` | Alpha derived from heartbeat interval (default = 20) |
| `rssi_avg` | `int8_t` | Exponential moving average (-127 = unknown) |
| `packets_rx` | `uint32_t` | Number of packets received. |
| `packets_sent` | `uint32_t` | Successfully transmitted over the air. |
| `driver_errors` | `uint32_t` | hal_esp_now_send() returned error (NO_MEM, etc.) |
| `delivery_failures` | `uint32_t` | ESP-NOW callback reported ESP_NOW_SEND_FAIL. |
| `packets_lost` | `uint32_t` | Number of packets lost (ACK timeout after retries) |
| `retries` | `uint32_t` | Number of retries. |
| `rtt_last_us` | `uint32_t` | Last round-trip time in microseconds. |
| `rtt_avg_us` | `uint32_t` | Average round-trip time in microseconds (0 = unknown) |

### `espnow::PeerStatisticsPersist`

Structure for persistent peer statistics.

| Field | Type | Description |
| :--- | :--- | :--- |
| `node_id` | `NodeId` | Node ID. |
| `rssi_avg` | `int8_t` | Exponential moving average. |
| `packets_rx` | `uint32_t` | Number of packets received. |
| `packets_sent` | `uint32_t` | Successfully transmitted over the air. |
| `driver_errors` | `uint32_t` | hal_esp_now_send() returned error |
| `delivery_failures` | `uint32_t` | ESP-NOW callback reported ESP_NOW_SEND_FAIL. |
| `packets_lost` | `uint32_t` | Number of packets lost (ACK timeout after retries) |
| `retries` | `uint32_t` | Number of retries. |
| `rtt_avg_us` | `uint32_t` | Average round-trip time in microseconds. |

### `espnow::PersistentPeer`

Peer information optimized for persistent storage (NVS/RTC).

| Field | Type | Description |
| :--- | :--- | :--- |
| `mac` | `uint8_t` | 6-byte MAC address |
| `type` | `NodeType` | Node type. |
| `node_id` | `NodeId` | Logical Node ID. |
| `paired` | `bool` | Pairing status. |
| `heartbeat_interval_ms` | `uint32_t` | Configured heartbeat interval. |

### `AckStatus`

Logical acknowledgment status codes returned by the application.

| Value | Description |
| :--- | :--- |
| `OK` | Message received and processed successfully. |
| `ERROR_INVALID_DATA` | Message received but payload data is invalid. |
| `ERROR_PROCESSING` | Message received but processing failed internally. |

### `NodeState`

Enumeration of node states.

NodeState transitions: in src/node_state_machine.cpp

| Value | Description |
| :--- | :--- |
| `UNINITIALIZED` | Initial state before initialization. |
| `IDLE` | Initialized successfully, but not yet paired/idle. |
| `PAIRING` | Actively advertising or accepting pairing requests. |
| `OPERATIONAL` | Has peers, normal operation. |
| `PAIRING_SCAN` | Scanning for a HUB to start pairing. |
| `RECOVERY_SCAN` | Lost connection to peers, rediscovering channel. |
| `COUNT` | Number of states (for validation) |

### `ChannelPolicy`

Controls whether the DiscoveryManager is allowed to change the WiFi channel when scanning for the hub.

Use SCAN when the node is not connected to any AP (standalone ESP-NOW only). Use FIXED when the node is connected to a WiFi AP: the channel is locked by the AP association and cannot be changed; both the hub and this node are assumed to be on the same AP channel.

Default: SCAN (preserves original behavior).

| Value | Description |
| :--- | :--- |
| `SCAN` | Node may call wifi_set_channel() during discovery scan (standalone mode) |
| `FIXED` | Node is WiFi-connected; channel is owned by the AP — no scanning allowed. |

---

## Public API (espnow::IEspNowManager)

Interface for the ESP-NOW Manager, providing high-level APIs for communication, peer management, and lifecycle.

This interface defines the contract for managing ESP-NOW communications in a structured way, supporting both HUB (central controller) and NODE (peripheral) roles.

### `init()`

**Initialize the ESP-NOW Manager.**

Sets up the necessary resources, including WiFi, ESP-NOW drivers, tasks, and queues. For HUB: Prepares to receive data from multiple nodes and manage the peer list. For NODE: Prepares to communicate with the HUB and optionally starts heartbeats.

```cpp
esp_err_t init(const EspNowConfig &config);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `config` | Configuration structure containing node ID, type, and resource settings. |

**Returns:**

* `ESP_OK on success`
* `ESP_ERR_INVALID_STATE: already initialized or wifi_set_mode == WIFI_MODE_NULL`
* `ESP_ERR_INVALID_ARG: app_rx_queue is null (not initialized and passed as argument)`
* `ESP_ERR_NO_MEM: memory allocation for mutex and internal queues fails`
* `ESP_FAIL: failed to create internal tasks`
* `Other: internal esp_now.h or esp_wifi.h errors`

> **Note**: This method must be called before any other operation.

> **Note**: On fail, deinit() is called automatically.

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

**Deinitialize the ESP-NOW Manager.**

Stops all background tasks, releases memory, and deinitializes the ESP-NOW driver.

```cpp
void deinit();
```

> **Note**: Idempotent if is already deinitialized.

> **Note**: This method does not return errors.

**Example:**
```cpp
manager.deinit();
```

---

### `set_channel_policy()`

**Set the WiFi channel scanning policy.**

Call with ChannelPolicy::FIXED when the node connects to a WiFi AP so that the discovery manager does not attempt to change the channel. Call with ChannelPolicy::SCAN when the node is not connected to any AP.

```cpp
void set_channel_policy(ChannelPolicy policy);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `policy` | ChannelPolicy::SCAN (default) or ChannelPolicy::FIXED |

**Example:**
```cpp
// When connected to a WiFi AP:
manager.set_channel_policy(espnow::ChannelPolicy::FIXED);

// If WiFi disconnects:
manager.set_channel_policy(espnow::ChannelPolicy::SCAN);
```

---

### `send_data()`

**Send data to a destination node.**

Encapsulates the payload into a standard message format and queues it for transmission. For HUB: Used to send application data to a specific registered node. For NODE: Typically used to send sensor data or status updates to the HUB.

```cpp
esp_err_t send_data(NodeId dest_node_id, PayloadType payload_type, const void *payload, size_t len, bool require_ack=false);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `dest_node_id` | ID of the destination node. |
| `payload_type` | Type identifier for the payload (application-defined). |
| `payload` | Pointer to the data buffer to be sent. |
| `len` | Length of the payload in bytes. |
| `require_ack` | If true, the calling task blocks until a logical acknowledgment is received or a failure occurs. |

**Returns:**

* `ESP_OK: packet sent successfully (if require_ack=true, logical ACK confirmed by destination node).`
* `ESP_ERR_INVALID_STATE: manager not in OPERATIONAL state, tx_queue not initialized, or manager stopped during wait.`
* `ESP_ERR_NOT_FOUND: the peer is not registered in peer storage.`
* `ESP_ERR_INVALID_ARG: payload length exceeds MAX_PAYLOAD_SIZE.`
* `ESP_ERR_TIMEOUT: require_ack=true and no logical ACK was received within the maximum retry duration.`
* `ESP_FAIL: failed to queue message, or maximum physical delivery failures reached (peer unreachable).`

> **Note**: If require_ack=false, this call is non-blocking and returns immediately after queueing.

> **Note**: If require_ack=true, the calling task blocks for up to (ack_timeout_ms * (logical_ack_retries + 1) + 200) ms.

> **Note**: Enters NodeState::RECOVERY_SCAN mode after MAX_FAILURES consecutive transmission failures.

> **Warning**: Maximum payload: 230 bytes (ESP-NOW limit - header - CRC)

**Example:**
```cpp
SensorData data = {.temperature = 25.5f, .humidity = 60};
esp_err_t err = manager.send_data(
    farm::NodeId::HUB,
    farm::PayloadType::WATER_LEVEL_REPORT,
    &data,
    sizeof(data),
    true // Require ACK
);
if (err == ESP_OK) {
    ESP_LOGI(TAG, "Data delivered and acknowledged");
}
```

---

### `send_data()`

**Template overload for send_data using enum types.**

```cpp
esp_err_t send_data(T1 dest_node_id, T2 payload_type, const void *payload, size_t len, bool require_ack=false);
```

---

### `send_command()`

**Send a command to a destination node.**

Similar to send_data, but specifically for control commands. For HUB: Used to control node behavior (e.g., change reporting interval). For NODE: Can be used to request actions from the HUB.

```cpp
esp_err_t send_command(NodeId dest_node_id, CommandType command_type, const void *payload, size_t len, bool require_ack=false);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `dest_node_id` | ID of the destination node. |
| `command_type` | Type of command to execute. |
| `payload` | Optional payload for the command. |
| `len` | Length of the payload. |
| `require_ack` | If true, the calling task blocks until a logical acknowledgment is received or a failure occurs. |

**Returns:**

* `ESP_OK: command sent successfully (if require_ack=true, logical ACK confirmed by destination node).`
* `ESP_ERR_INVALID_STATE: manager not in OPERATIONAL state, tx_queue not initialized, or manager stopped during wait.`
* `ESP_ERR_NOT_FOUND: the peer is not registered in peer storage.`
* `ESP_ERR_INVALID_ARG: payload length exceeds MAX_PAYLOAD_SIZE.`
* `ESP_ERR_TIMEOUT: require_ack=true and no logical ACK was received within the maximum retry duration.`
* `ESP_FAIL: failed to queue message, or maximum physical delivery failures reached (peer unreachable).`

> **Note**: If require_ack=false, this call is non-blocking and returns immediately after queueing.

> **Note**: If require_ack=true, the calling task blocks for up to (ack_timeout_ms * (logical_ack_retries + 1) + 200) ms.

> **Note**: Enters NodeState::RECOVERY_SCAN mode after MAX_FAILURES consecutive transmission failures.

> **Warning**: Maximum payload: 230 bytes (ESP-NOW limit - header - CRC)

**Example:**
```cpp
farm::LoadOnCommand cmd{.circuit_id = 0, .power_source = farm::PowerSource::SOLAR, .watchdog_timeout_s = 600};
esp_err_t err = manager.send_command(
    farm::NodeId::PUMP_CONTROL,
    farm::CommandType::LOAD_ON,
    &cmd,
    sizeof(cmd),
    true // Require ACK
);
```

---

### `send_command()`

**Template overload for send_command using enum for NodeId.**

```cpp
esp_err_t send_command(T dest_node_id, CommandType command_type, const void *payload, size_t len, bool require_ack=false);
```

---

### `confirm_reception()`

**Confirm the reception of a message that required an ACK.**

Sends a logical acknowledgment back to the specified sender. This should be called by the application after processing a received message that had the require_ack flag set, to inform the sender of the processing outcome.

```cpp
esp_err_t confirm_reception(NodeId sender_id, uint16_t sequence_number, AckStatus status);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `sender_id` | Logical ID of the sender node to acknowledge. |
| `sequence_number` | Sequence number of the original message being acknowledged. |
| `status` | Processing outcome: AckStatus::OK for success, AckStatus::ERROR_INVALID_DATA for invalid payload, or AckStatus::ERROR_PROCESSING for internal errors. |

**Returns:**

* `ESP_OK: ACK was queued successfully.`
* `ESP_ERR_INVALID_STATE: manager not in OPERATIONAL/PAIRING state, or tx_queue not initialized.`
* `ESP_ERR_NOT_FOUND: peer MAC not found for the specified sender_id.`
* `ESP_FAIL: failed to queue ACK packet for transmission.`

**Example:**
```cpp
if (msg.requires_ack) {
    manager.confirm_reception(msg.sender_id, msg.sequence_number, espnow::AckStatus::OK);
}
```

---

### `add_peer()`

**Manually add a peer to the manager.**

Registers a node in the internal peer list and adds it to the ESP-NOW driver's peer table.

```cpp
esp_err_t add_peer(NodeId node_id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `node_id` | Unique ID of the node. |
| `mac` | MAC address of the node (6 bytes). |
| `type` | Role/Type of the node. |
| `heartbeat_interval_ms` | Heartbeat interval in milliseconds. |

**Returns:**

* `ESP_OK: on success.`
* `ESP_ERR_INVALID_ARG: mac is nullptr.`
* `ESP_ERR_TIMEOUT: failed to acquire mutex within timeout.`
* `ESP_ERR_NO_MEM: ESP-NOW driver failed to allocate memory for peer.`
* `ESP_ERR_ESPNOW_NOT_INIT: ESP-NOW driver not initialized.`
* `ESP_ERR_ESPNOW_ARG: invalid argument passed to ESP-NOW driver.`
* `ESP_ERR_ESPNOW_NO_MEM: ESP-NOW driver out of memory.`
* `ESP_ERR_ESPNOW_NOT_FOUND: peer not found when updating existing peer.`
* `ESP_ERR_ESPNOW_CHAN: invalid WiFi channel.`
* `ESP_ERR_ESPNOW_IF: invalid interface.`
* `ESP_ERR_WIFI_NOT_INIT: WiFi not initialized.`
* `ESP_ERR_WIFI_NOT_STARTED: WiFi not started.`
* `ESP_ERR_WIFI_ARG: invalid WiFi argument.`
* `ESP_ERR_INVALID_STATE: storage failed to persist peer data.`

> **Note**: List uses LRU (Least Recently Used) policy with maximum MAX_PEERS = 19 (ESP-NOW limitation)

> **Note**: When full, oldest peer (least recently used) is removed to make room

> **Note**: Re-adding existing peer moves it to front (marks as recently used)

> **Note**: Automatically persisted to RTC and NVS storage if is a new peer or readding one with different mac or wifi channel

> **Warning**: ESP-NOW hardware limit is 20 peers, but 1 is reserved for broadcast

**Example:**
```cpp
uint8_t pump_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
esp_err_t err = manager.add_peer(
    farm::NodeId::PUMP_CONTROL,
    pump_mac,
    farm::NodeType::ACTUATOR,
    30000 // 30s heartbeat
);
```

---

### `add_peer()`

**Template overload for add_peer using enums.**

```cpp
esp_err_t add_peer(T1 node_id, const uint8_t *mac, T2 type, uint32_t heartbeat_interval_ms);
```

---

### `remove_peer()`

**Remove a peer from the manager.**

Removes the peer from both internal lists and the ESP-NOW driver.

```cpp
esp_err_t remove_peer(NodeId node_id);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `node_id` | ID of the node to remove. |

**Returns:**

* `ESP_OK: on success.`
* `ESP_ERR_NOT_FOUND: the peer is not present.`
* `ESP_ERR_TIMEOUT: failed to acquire mutex within timeout.`
* `ESP_ERR_ESPNOW_NOT_INIT: ESP-NOW driver not initialized.`
* `ESP_ERR_ESPNOW_ARG: invalid argument passed to ESP-NOW driver.`
* `ESP_ERR_ESPNOW_NOT_FOUND: peer not found in ESP-NOW driver.`
* `ESP_ERR_ESPNOW_CHAN: invalid WiFi channel.`
* `ESP_ERR_ESPNOW_IF: invalid interface.`
* `ESP_ERR_WIFI_NOT_INIT: WiFi not initialized.`
* `ESP_ERR_WIFI_NOT_STARTED: WiFi not started.`
* `ESP_ERR_WIFI_ARG: invalid WiFi argument.`
* `ESP_ERR_INVALID_STATE: storage failed to persist peer removal.`

**Example:**
```cpp
esp_err_t err = manager.remove_peer(farm::NodeId::PUMP_CONTROL);
if (err == ESP_ERR_NOT_FOUND) {
    ESP_LOGW(TAG, "Peer was not registered");
}
```

---

### `remove_peer()`

**Template overload for remove_peer using enum.**

```cpp
esp_err_t remove_peer(T node_id);
```

---

### `get_peer()`

**Get information for a specific peer.**

```cpp
bool get_peer(NodeId node_id, PeerInfo &out);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `node_id` | The logical ID of the peer. |
| `out` | Output parameter populated with peer info if found. |

**Returns:**

* `true if peer was found and out was populated.`
* `false if peer is not registered or mutex acquisition fails.`

**Example:**
```cpp
espnow::PeerInfo info{};
if (manager.get_peer(farm::NodeId::PUMP_CONTROL, info)) {
    ESP_LOGI(TAG, "Heartbeat interval: %lu ms", static_cast<unsigned long>(info.heartbeat_interval_ms));
}
```

---

### `get_peer()`

**Template overload for get_peer using enum for NodeId.**

```cpp
bool get_peer(T node_id, PeerInfo &out);
```

---

### `has_peer()`

**Checks if a peer is registered in the peer list.**

Unlike is_peer_online(), this returns true if the peer is registered/paired regardless of whether it is currently awake or in deep sleep.

```cpp
bool has_peer(NodeId node_id) const;
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `node_id` | Logical ID of the node to check. |

**Returns:**

* `true if the peer is registered.`
* `false if the peer is not registered or mutex acquisition fails.`

**Example:**
```cpp
if (manager.has_peer(farm::NodeId::WEATHER)) {
    ESP_LOGI(TAG, "Weather station is registered");
}
```

---

### `has_peer()`

**Template overload for has_peer using enum for NodeId.**

```cpp
bool has_peer(T node_id) const;
```

---

### `get_peer_count()`

**Get the number of currently registered peers.**

```cpp
size_t get_peer_count() const;
```

**Returns:**

* `Total count of registered peers.`

**Example:**
```cpp
size_t count = manager.get_peer_count();
ESP_LOGI(TAG, "Active registered peers: %zu", count);
```

---

### `get_peers()`

**Get a list of all registered peers.**

```cpp
etl::vector< PeerInfo, MAX_PEERS > get_peers();
```

**Returns:**

* `Vector containing information for all registered peers.`

> **Note**: This method does not return errors. Returns empty vector if mutex acquisition fails.

**Example:**
```cpp
auto peers = manager.get_peers();
for (const auto& peer : peers) {
    ESP_LOGI(TAG, "Peer ID: 0x%02X", peer.node_id);
}
```

---

### `get_peer_stats()`

**Get statistics for a specific peer.**

```cpp
bool get_peer_stats(NodeId node_id, PeerStatistics &out) const;
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `node_id` | The logical ID of the peer. |
| `out` | Output parameter filled with current statistics. |

**Returns:**

* `true if the peer was found and out was populated.`
* `false if the peer is not tracked or stats not yet available.`

**Example:**
```cpp
espnow::PeerStatistics stats{};
if (manager.get_peer_stats(farm::NodeId::WATER_TANK, stats)) {
    ESP_LOGI(TAG, "RSSI: %d dBm | Pkt Loss: %.1f%%", stats.last_rssi, stats.packet_loss_percent);
}
```

---

### `get_peer_stats()`

**Template overload for get_peer_stats using enum for NodeId.**

```cpp
bool get_peer_stats(T node_id, PeerStatistics &out) const;
```

---

### `get_all_peer_stats()`

**Get statistics for all tracked peers.**

```cpp
etl::vector< PeerStatistics, MAX_PEERS > get_all_peer_stats() const;
```

**Returns:**

* `Vector of PeerStatistics. Empty if no peers are tracked.`

> **Note**: This method does not return errors.

**Example:**
```cpp
auto all_stats = manager.get_all_peer_stats();
for (const auto& s : all_stats) {
    ESP_LOGI(TAG, "Node 0x%02X: RX %lu, TX %lu", s.node_id, s.packets_rx, s.packets_tx);
}
```

---

### `get_offline_peers()`

**Get a list of IDs for peers considered offline.**

A peer is considered offline if no heartbeat has been received within its expected interval multiplied by HEARTBEAT_OFFLINE_MULTIPLIER.

```cpp
etl::vector< NodeId, MAX_PEERS > get_offline_peers() const;
```

**Returns:**

* `Vector of Node IDs. Returns empty vector if mutex acquisition fails or manager not operational.`

> **Note**: This method does not return errors.

**Example:**
```cpp
auto offline = manager.get_offline_peers();
for (auto id : offline) {
    ESP_LOGW(TAG, "Node 0x%02X is offline", id);
}
```

---

### `is_peer_online()`

**Checks if a specific peer is currently considered online.**

A peer is considered online if it is registered, has been heard from in the current session, and the elapsed time since its last message does not exceed its configured heartbeat interval multiplied by HEARTBEAT_OFFLINE_MULTIPLIER.

```cpp
bool is_peer_online(NodeId node_id) const;
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `node_id` | Logical ID of the node to check. |

**Returns:**

* `true if the peer is registered, active, and within its timeout window.`
* `false if the peer is unknown, has never sent a message, has no heartbeat interval, or timed out.`

> **Note**: This method does not return errors. Returns false if manager is not operational or pairing.

**Example:**
```cpp
if (!manager.is_peer_online(farm::NodeId::PUMP_CONTROL)) {
    ESP_LOGW(TAG, "Pump controller unreachable");
}
```

---

### `is_peer_online()`

**Template overload for is_peer_online using enum for NodeId.**

```cpp
bool is_peer_online(T node_id) const;
```

---

### `start_pairing()`

**Start the pairing process.**

For HUB:
Enters listening mode for pairing requests


Accepts requests from any node broadcasting


Automatically adds responding peers to peer list

For NODE:
Broadcasts pairing request periodically (every 1s)


Waits for HUB to respond with acknowledgment


Automatically stops when paired or timeout reached

```cpp
esp_err_t start_pairing(uint32_t timeout_ms=30000);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `timeout_ms` | Duration of the pairing mode in milliseconds. |

**Returns:**

* `ESP_OK: pairing started successfully.`
* `ESP_ERR_INVALID_STATE: manager is UNINITIALIZED or pairing already active.`

> **Note**: Automatic stop after specified timeout duration

> **Warning**: Both HUB and NODE must be in pairing mode simultaneously

**Example:**
```cpp
// Start pairing for 60 seconds:
esp_err_t err = manager.start_pairing(60000);
```

---

### `reconnect()`

**Attempt to reconnect after scan exhaustion.**

Resets the retry counter and immediately triggers a RECOVERY_SCAN. Intended to be called by the application when the node is in IDLE state due to exhausted scan retries. Semantically different from start_pairing(): reconnect assumes the HUB ID is already and in the peer list and the node just needs to find the HUB channel.

```cpp
esp_err_t reconnect();
```

**Returns:**

* `ESP_OK on success`
* `ESP_ERR_INVALID_STATE if node is not in IDLE state`
* `ESP_ERR_INVALID_ARG if there are no peers`

**Example:**
```cpp
if (manager.get_node_state() == espnow::NodeState::IDLE) {
    manager.reconnect();
}
```

---

### `set_enable_heartbeat()`

**Enables or disables autonomous heartbeat packet generation at runtime.**

```cpp
void set_enable_heartbeat(bool enable);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `enable` | True to enable autonomous heartbeat sending, false to disable. |

> **Note**: This method does not return errors. Does nothing if manager is uninitialized.

**Example:**
```cpp
manager.set_enable_heartbeat(false); // Pause heartbeats before entering sleep
```

---

### `is_heartbeat_enabled()`

**Checks if autonomous heartbeat packet generation is currently enabled.**

```cpp
bool is_heartbeat_enabled() const;
```

**Returns:**

* `True if heartbeat generation is enabled and manager is initialized.`

> **Note**: This method does not return errors.

**Example:**
```cpp
if (manager.is_heartbeat_enabled()) {
    ESP_LOGI(TAG, "Heartbeats active");
}
```

---

### `set_heartbeat_interval_ms()`

**Sets the heartbeat generation interval in milliseconds at runtime.**

```cpp
void set_heartbeat_interval_ms(uint32_t interval_ms);
```

**Parameters:**

| Parameter | Description |
| :--- | :--- |
| `interval_ms` | Interval in milliseconds between autonomous heartbeat packets. |

> **Note**: This method does not return errors. Does nothing if manager is uninitialized.

**Example:**
```cpp
manager.set_heartbeat_interval_ms(15000); // Send heartbeat every 15s
```

---

### `get_node_state()`

**Get the current node state.**

```cpp
NodeState get_node_state() const;
```

**Returns:**

* `The current node state.`

> **Note**: This method does not return errors.

**Example:**
```cpp
if (manager.get_node_state() == espnow::NodeState::OPERATIONAL) {
    // Link is ready
}
```

---

### `is_initialized()`

**Check if EspNowManager is initialized.**

```cpp
bool is_initialized() const;
```

**Returns:**

* `true if initialized, false otherwise.`

> **Note**: This method does not return errors.

**Example:**
```cpp
if (!manager.is_initialized()) {
    manager.init(config);
}
```

---
