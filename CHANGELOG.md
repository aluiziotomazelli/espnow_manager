# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.5.0] - 2026-08-26

### Added
- `get_peer(NodeId, PeerInfo&)` method in `IEspNowManager` and `EspNowManager` (with enum template overload) for $O(N)$ single peer lookup by reference, avoiding full vector copy allocations.
- `has_peer(NodeId)` method in `IEspNowManager` and `EspNowManager` (with enum template overload) for fast boolean peer existence checks.
- `get_peer_count()` method in `IEspNowManager` and `EspNowManager` to query current registered peer count without allocating or copying vectors.
- Corresponding `get(NodeId, PeerInfo&)`, `has_peer(NodeId)`, and `get_peer_count()` methods in `IPeerManager` and `PeerManager`.
- Automated Markdown API generator script (`scripts/generate_api_md.py`) using Doxygen XML output with rich code examples.

### Changed
- Moved `AckStatus` enum from internal `protocol_types.hpp` to public `espnow_types.hpp` header so applications can explicitly construct ACK responses.

## [1.4.0] - 2026-08-20

### Added
- `scan_max_backoff_ms` field in `EspNowConfig` (defaults to `SCAN_MAX_BACKOFF_MS = 300000` ms / 5 minutes) to cap the maximum interval between recovery channel scans.
- `MAX_BACKOFF_SHIFT_BITS` constant to protect against 32-bit integer overflow in exponential backoff calculation.

### Changed
- Replaced finite recovery scan retry limit (`SCAN_MAX_RETRIES`) with persistent, capped exponential backoff. Nodes with known peers in storage no longer give up and stall in `IDLE` after transient network outages or Hub reboots; they automatically and indefinitely attempt to rediscover the Hub at the capped backoff interval.

## [1.3.0] - 2026-08-19

### Added
- `is_peer_online(NodeId node_id)` method in `IEspNowManager` and `EspNowManager` (with enum template overload) for $O(N)$ direct link health verification without vector allocation.
- `is_online(NodeId id, int64_t now_ms)` method in `IPeerManager` and `PeerManager` implementing strict timeout calculation against contractual `heartbeat_interval_ms * HEARTBEAT_OFFLINE_MULTIPLIER`.
- `enable_heartbeat` flag in `EspNowConfig` (defaults to `true`), `IHeartbeatManager`, and `HeartbeatManager` to decouple the heartbeat timeout contract (sent during pairing) from autonomous background `HEARTBEAT` packet generation.
- `set_enable_heartbeat(bool enable)`, `is_heartbeat_enabled()`, and `set_heartbeat_interval_ms(uint32_t interval_ms)` in `IEspNowManager` and `EspNowManager` for runtime control of heartbeat generation.

### Changed
- Standardized `EspNowConfig` with modern in-class member initializers, removing redundant constructor logic and enabling pure C++ aggregate initialization.

### Fixed
- Fixed spurious `Stats CRC mismatch` on cold boot by validating RTC memory integrity in `StorageManager::is_data_dirty()` before relying on logical field comparison (closes #20).

### Documentation
- Clarified difference between contractual timeout (`heartbeat_interval_ms`) and autonomous emission (`enable_heartbeat`) in Doxygen headers and `README.md`.
- Added link health verification examples using `is_peer_online()` to `README.md`.

## [1.2.2] - 2026-08-07

### Added
- `logical_ack_retries` field in `EspNowConfig` (defaults to `0`). Controls how many times `TxManager` retransmits a packet on L7 logical ACK timeout when physical MAC delivery has succeeded.

### Fixed
- **HUB State Machine**: Updated `on_scan_requested(bool is_hub)` to keep HUB nodes in `OPERATIONAL` state when `NOTIFY_MAX_FAILURES` occurs (e.g. when transmitting to a sleeping or unreachable peer node). Prevents HUB nodes from getting stuck in `RECOVERY_SCAN` and returning `ESP_ERR_INVALID_STATE`.

## [1.2.1] - 2026-08-04

### Changed
- `send_data()` and `send_command()` in `IEspNowManager` / `EspNowManager` now block the calling task when `require_ack = true` for up to `(ack_timeout_ms * (MAX_FAILURES + 1) + 200)` ms, returning explicit status (`ESP_OK`, `ESP_ERR_TIMEOUT`, `ESP_FAIL`, `ESP_ERR_INVALID_STATE`).
- Updated `TxManager::queue_packet` to create a dedicated FreeRTOS `EventGroup` per reliable transmission and block synchronously until logical ACK confirmation, ACK timeout, or system teardown.

### Documentation
- Updated Doxygen comments in `include/interfaces/i_espnow_manager.hpp` and `API.md` with blocking behavior specifications, timeout calculations, and complete return code mappings.

## [1.2.0] - 2026-07-20

### Added
- `ChannelPolicy` enum (`SCAN`, `FIXED`) to control WiFi channel management behavior during discovery scans.
- `set_channel_policy()` method in `IEspNowManager` and `EspNowManager` to switch between dynamic channel scanning (`SCAN`) and fixed channel mode (`FIXED`).

### Changed
- Removed automatic `wifi_set_channel()` call from `EspNowDriver::init()`. Setting the WiFi channel is now the responsibility of the application (or WiFi Manager).
- `EspNowConfig::wifi_channel` now serves strictly as the initial starting point for scanning in `SCAN` mode, rather than configuring hardware channel state upon initialization.

### Documentation
- Documented `ChannelPolicy` usage, simultaneous WiFi STA + ESP-NOW coexistence, and channel ownership guidelines in `README.md` and `API.md`.

## [1.1.2] - 2026-05-01
### Fixed
- DiscoveryManager: Add device ID check to handle_scan_response to prevent unintended node state transitions.
 - 2026-05-01

### Fixed
- Correct discovery task initialization parameters and add config defaults.

### Added
- Created `stack_stress_test` application to validate FreeRTOS task memory usage under extreme network conditions.
- Documented Memory and Task Stack Tuning guidelines in README based on High Water Mark analysis.
- Optimized `stack_size_discovery_task` to `3072` bytes to free up system memory while maintaining a safe operating margin.

## [1.1.0] - 2026-04-26

### Breaking Changes
- Encapsulated the entire component within `namespace espnow`.
- All public APIs, types, and internal classes are now namespaced.
- Updated all test suites and test applications to use the namespaced API.

### Added
- Added `namespace espnow` to all public headers.
- Updated build system and documentation to reflect the namespace change.

[1.1.0]: https://github.com/aluiziotomazelli/espnow_manager/releases/tag/v1.1.0

## [1.0.1] - 2026-04-24

### Fixed
- Fixed dependency resolution for the ETL library when using `espnow_manager` as an external component (e.g., via `idf_component.yml`).
- Integrated ETL directly into the component's build process using `FetchContent`, eliminating the need for separate `EXTRA_COMPONENT_DIRS` configuration in consumer applications.

[1.0.1]: https://github.com/aluiziotomazelli/espnow_manager/releases/tag/v1.0.1

## [1.0.0] - 2026-04-15

### Architecture

The `espnow_manager` component was built using a **Facade + Decentralized Managers** pattern to ensure a clean separation of concerns and high testability. By abstracting all hardware dependencies (WiFi, ESP-NOW, FreeRTOS, and Timers) through interfaces, the core protocol logic is 100% decoupled from the ESP-IDF C APIs.

A central **`rx_task`** acts as the decision-making hub, receiving direct task notifications from specialized managers to drive the global **`NodeStateMachine`**. This design allows for a single point of truth regarding the node's state (e.g., IDLE, PAIRING, OPERATIONAL, RECOVERY) while delegating specific protocol logic to dedicated components.

### Added
- Initial release of ESP-NOW Manager component for ESP32 (C++17).
- Star topology support with dedicated HUB and NODE roles.
- **HAL Abstraction Layer**: Interfaces for WiFi, ESP-NOW, FreeRTOS, and Timers to enable host-based mocking.
- **Node State Machine**: Governance of device states, including automatic recovery and pairing flows.
- **Reliable Transmission**: `TxManager` with structured encoding (CRC16), sequence numbering, and logical ACK/NACK protocol.
- **Automatic Discovery**: Multi-channel scanning to find and pair with the HUB without hardcoded MAC addresses.
- **Link Health Monitoring**: Heartbeat-based connection tracking with automatic offline detection.
- **Channel Recovery**: Intelligent multi-channel scanning when connection is lost (e.g., HUB changed channel).
- **Dual Persistence**: Automatic synchronization of peer data between RTC RAM (for fast wake-from-deep-sleep) and NVS.
- **Network Statistics**: Per-peer tracking of RSSI, RTT (Exponential Moving Averages), and packet delivery metrics.
- **Host-Based Testing**: Comprehensive test suite using GoogleTest/GoogleMock with 100% logic coverage.

[1.0.1]: https://github.com/aluiziotomazelli/espnow_manager/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/aluiziotomazelli/espnow_manager/releases/tag/v1.0.0

