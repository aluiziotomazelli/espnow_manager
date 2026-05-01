# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.1] - 2026-05-01

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

