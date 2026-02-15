# ESP-NOW Manager API Reference

This document provides a detailed reference for the ESP-NOW Manager component API, including the public interface, data structures, and protocol types.

## Public Interface: `IEspNowManager`

The `IEspNowManager` class defines the contract for managing ESP-NOW communications. It supports both HUB (central controller) and NODE (peripheral) roles.

### Lifecycle Management

#### `init`
Initializes the ESP-NOW Manager, setting up WiFi, drivers, tasks, and queues.

**Parameters:**
- `config`: `const EspNowConfig &`
  - Configuration structure containing node ID, type, and resource settings.

**Returns:**
- `ESP_OK`:
  - Initialization successful.
- `ESP_ERR_INVALID_STATE`:
  - Manager is already initialized or WiFi mode is not set (must be STA or AP+STA).
- `ESP_ERR_INVALID_ARG`:
  - The `app_rx_queue` pointer in the configuration is null.
- `ESP_ERR_NO_MEM`:
  - Memory allocation for internal mutex or queues failed.
- `ESP_FAIL`:
  - Failed to create internal tasks.
- `Other`:
  - Internal errors propagated from `esp_now_init` or `esp_wifi` functions.

#### `deinit`
Deinitializes the ESP-NOW Manager, stopping background tasks and releasing all allocated resources.

**Returns:**
- `ESP_OK`:
  - Deinitialization successful (operation is idempotent).

---

### Data Communication

#### `send_data`
Encapsulates application data into a standard message format and queues it for transmission.

**Parameters:**
- `dest_node_id`: `NodeId`
  - Logical ID of the destination node.
- `payload_type`: `PayloadType`
  - Application-defined identifier for the payload type.
- `payload`: `const void *`
  - Pointer to the data buffer to be sent.
- `len`: `size_t`
  - Length of the payload in bytes (max 230 bytes).
- `require_ack`: `bool`
  - If true, the system will track logical acknowledgment in the background.

**Returns:**
- `ESP_OK`:
  - The packet was successfully queued for transmission.
- `ESP_ERR_NOT_FOUND`:
  - The destination node ID is not in the registered peer list.
- `ESP_ERR_INVALID_ARG`:
  - The payload is empty or encoding failed.
- `ESP_ERR_INVALID_STATE`:
  - The manager or internal transmission manager is not initialized.
- `ESP_FAIL`:
  - The internal transmission queue is full.

**Note:** This operation is asynchronous and non-blocking. The logical ACK status (if requested) is handled by the internal TX Manager task. If `require_ack` is false, the application is not notified of delivery failures.

#### `send_command`
Sends a control command to a destination node. Similar to `send_data` but specialized for commands.

**Parameters:**
- `dest_node_id`: `NodeId`
  - Logical ID of the destination node.
- `command_type`: `CommandType`
  - Type of command to execute (e.g., REBOOT, START_OTA).
- `payload`: `const void *`
  - Optional payload for the command. Can be `nullptr` if `len` is 0.
- `len`: `size_t`
  - Length of the optional payload. Can be 0 if no additional data is required.
- `require_ack`: `bool`
  - If true, the system will track logical acknowledgment in the background.

**Returns:**
- `ESP_OK`:
  - The packet was successfully queued for transmission.
- `ESP_ERR_NOT_FOUND`:
  - The destination node ID is not in the registered peer list.
- `ESP_ERR_INVALID_ARG`:
  - The payload encoding failed.
- `ESP_ERR_INVALID_STATE`:
  - The manager is not initialized.
- `ESP_FAIL`:
  - The internal transmission queue is full.

**Note:** If `require_ack` is false, the application is not notified of delivery failures.

#### `confirm_reception`
Sends a logical acknowledgment back to the sender of the last received message that had the `require_ack` flag set.

**Parameters:**
- `status`: `AckStatus`
  - Status of the processing (e.g., OK, ERROR_INVALID_DATA, or ERROR_PROCESSING).

**Returns:**
- `ESP_OK`:
  - ACK was successfully queued for transmission.
- `ESP_ERR_INVALID_STATE`:
  - No message is currently pending an ACK.
- `ESP_ERR_TIMEOUT`:
  - Failed to acquire the internal mutex within the 100ms timeout.
- `ESP_ERR_NOT_FOUND`:
  - The sender of the message being acknowledged is no longer in the peer list. (Occurs if the peer is removed between reception and confirmation).
- `ESP_FAIL`:
  - Internal encoding error or the transmission queue is full.

---

### Peer Management

#### `add_peer`
Manually registers a node in the internal peer list and the ESP-NOW driver's peer table.

**Parameters:**
- `node_id`: `NodeId`
  - Unique logical ID assigned to the node.
- `mac`: `const uint8_t *`
  - 6-byte MAC address of the node.
- `channel`: `uint8_t`
  - WiFi channel the node is operating on.
- `type`: `NodeType`
  - Role/Type of the node (HUB or peripheral).

**Returns:**
- `ESP_OK`:
  - Peer added or updated successfully.
- `ESP_ERR_INVALID_ARG`:
  - The MAC address pointer is null.
- `Other`:
  - Internal errors from the ESP-NOW driver or storage persistence.

**Note:** This method blocks indefinitely until the internal peer list mutex is acquired. It uses an LRU (Least Recently Used) policy when the peer list is full (max 19 peers).

#### `remove_peer`
Removes a peer from both internal lists and the ESP-NOW driver.

**Parameters:**
- `node_id`: `NodeId`
  - ID of the node to remove.

**Returns:**
- `ESP_OK`:
  - Peer removed successfully.
- `ESP_ERR_NOT_FOUND`:
  - The node ID is not present in the peer list.

**Note:** This method blocks indefinitely until the internal peer list mutex is acquired.

#### `get_peers`
Retrieves a copy of the list of all registered peers.

**Returns:**
- `std::vector<PeerInfo>`:
  - Vector containing detailed information for all registered peers.

#### `get_offline_peers`
Retrieves a list of IDs for peers considered offline based on heartbeat timeout.

**Returns:**
- `std::vector<NodeId>`:
  - Vector of Node IDs for peers that haven't sent a heartbeat within the expected interval.

---

### Pairing

#### `start_pairing`
Starts the pairing process for a specified duration.

**Parameters:**
- `timeout_ms`: `uint32_t`
  - Duration of the pairing mode in milliseconds (default: 30000ms).

**Returns:**
- `ESP_OK`:
  - Pairing mode started successfully.
- `ESP_ERR_INVALID_STATE`:
  - Pairing is already active.

---

### Status

#### `is_initialized`
Checks if the manager has been successfully initialized.

**Returns:**
- `bool`:
  - `true` if initialized, `false` otherwise.

## Data Structures

### `EspNowConfig`
Initialization configuration.

| Member | Type | Description |
| :--- | :--- | :--- |
| `node_id` | `NodeId` | Logical ID for this device |
| `node_type` | `NodeType` | Role/Type for this device |
| `app_rx_queue` | `QueueHandle_t` | Application queue where incoming DATA/COMMANDS are posted |
| `wifi_channel` | `uint8_t` | Initial WiFi channel to operate on |
| `ack_timeout_ms` | `uint32_t` | Timeout for logical acknowledgments (ms) |
| `heartbeat_interval_ms` | `uint32_t` | Interval for heartbeats; 0 disables generation (ms) |
| `stack_size_rx_dispatch` | `uint32_t` | Stack size for the internal packet dispatcher task |
| `stack_size_transport_worker` | `uint32_t` | Stack size for the worker task |
| `stack_size_tx_manager` | `uint32_t` | Stack size for the transmission manager task |
| `priority_rx_dispatch` | `UBaseType_t` | Priority for the internal packet dispatcher task |
| `priority_transport_worker`| `UBaseType_t` | Priority for the worker task |
| `priority_tx_manager` | `UBaseType_t` | Priority for the transmission manager task |

### `PeerInfo`
Detailed information about a registered peer.

| Member | Type | Description |
| :--- | :--- | :--- |
| `mac` | `uint8_t[6]` | 6-byte MAC address |
| `type` | `NodeType` | Node role/category |
| `node_id` | `NodeId` | Unique logical ID |
| `channel` | `uint8_t` | Current WiFi channel |
| `last_seen_ms` | `uint64_t` | Timestamp of the last message received (ms) |
| `paired` | `bool` | True if the node is successfully paired |
| `heartbeat_interval_ms` | `uint32_t` | Expected frequency of heartbeat messages (ms) |

### `RxPacket`
Structure for packets received from the network.

| Member | Type | Description |
| :--- | :--- | :--- |
| `src_mac` | `uint8_t[6]` | Source MAC address |
| `data` | `uint8_t[250]` | Raw payload data (including protocol headers) |
| `len` | `size_t` | Length of the payload in bytes |
| `rssi` | `int8_t` | Signal strength indicator (dBm) |
| `timestamp_us` | `int64_t` | Microsecond timestamp of reception |

## Types and Constants

### Protocol Identifiers
- `NodeId`: `uint8_t` (0-255)
- `NodeType`: `uint8_t`
- `PayloadType`: `uint8_t`

### Enumerations

#### `MessageType`
| Value | Description |
| :--- | :--- |
| `PAIR_REQUEST` | Initial request to pair |
| `PAIR_RESPONSE` | Response to a pairing request |
| `HEARTBEAT` | Periodic keep-alive message |
| `HEARTBEAT_RESPONSE`| Acknowledgment of heartbeat |
| `DATA` | Standard application data packet |
| `ACK` | Logical acknowledgment for DATA or COMMAND |
| `COMMAND` | Control command sent from HUB to NODE |
| `CHANNEL_SCAN_PROBE` | Broadcast probe for discovery |
| `CHANNEL_SCAN_RESPONSE`| Response to a discovery probe |

#### `AckStatus`
| Value | Description |
| :--- | :--- |
| `OK` | Successfully processed |
| `ERROR_INVALID_DATA` | Received but payload is invalid |
| `ERROR_PROCESSING` | Received but internal failure occurred |

#### `CommandType`
| Value | Description |
| :--- | :--- |
| `START_OTA` | Instructs to start firmware update |
| `REBOOT` | Instructs to perform a system reset |
| `SET_REPORT_INTERVAL` | Instructs to change reporting frequency |

### Reserved Values

#### `ReservedIds`
- `BROADCAST`: `0xFF`
- `HUB`: `0x01`

#### `ReservedTypes`
- `UNKNOWN`: `0x00`
- `HUB`: `0x01`

## System Constants
| Constant | Value | Description |
| :--- | :--- | :--- |
| `MAX_PEERS` | 19 | Maximum number of registered peers |
| `MAX_PAYLOAD_SIZE`| 233 | Maximum application payload size (bytes) |
| `MESSAGE_HEADER_SIZE`| 16 | Universal protocol header size (bytes) |
| `CRC_SIZE` | 1 | CRC field size (bytes) |
| `DEFAULT_WIFI_CHANNEL`| 1 | Default channel if none specified |
| `MAX_LOGICAL_RETRIES`| 3 | Max attempts for unacknowledged packets |
