# Host Tests

This directory contains Linux-based host tests for the EspNow component, providing a robust testing environment that doesn't require physical hardware.

## Test Scope
The tests cover individual components (Unit Tests) and their interactions (Integration Tests). We aim for high coverage across all critical communication logic, including:
- Protocol encoding/decoding and CRC validation.
- Peer management and persistent storage.
- Transmission state machine and retry logic.
- Heartbeat monitoring and node health tracking.
- Pairing process and channel discovery.
- Message routing and dispatching.

## Mocks and Abstractions

To isolate the logic under test, we use several mocking strategies:

### 1. ESP-IDF Mocks
We leverage the ESP-IDF host testing framework to mock standard components like `esp_wifi` and `esp_now`. These mocks simulate the behavior of the ESP32 hardware and drivers.

### 2. CMock Generated Mocks
Mocks generated automatically by CMock from C headers. These are primarily used for low-level IDF APIs, allowing us to expect specific function calls and return predetermined values.

### 3. Custom Mocks (`/mocks`)
Located in the root `mocks/` directory, these are hand-written C++ mocks for our own internal interfaces (e.g., `IPeerManager`, `ITxManager`). They provide:
- **Call Spying:** Track if methods were called and with what arguments.
- **Stubbing:** Configure complex return values or behaviors for specific test scenarios.
- **State Simulation:** Simulate internal state changes (like task handles or queue statuses).

## Directory Structure
- `peer_manager/`: Tests for the `RealPeerManager` class.
- `tx_state_machine/`: Tests for the `RealTxStateMachine` class.
- `channel_scanner/`: Tests for the `RealChannelScanner` class.
- `espnow_storage/`: Tests for the `EspNowStorage` and its backends.
- `heartbeat_manager/`: Tests for the `RealHeartbeatManager`.
- `message_codec/`: Tests for the `RealMessageCodec`.
- `message_router/`: Tests for the `RealMessageRouter`.
- `pairing_manager/`: Tests for the `RealPairingManager`.
- `tx_manager/`: Tests for the `RealTxManager`.
- `wifi_hal/`: Tests for the `RealWiFiHAL`.
- `espnow_manager_main/`: Integration tests for the full `EspNowManager` assembly.
- `espnow_manager_singleton/`: Specific tests for singleton lifecycle and static callbacks.
- `utils/`: Common test utilities and helpers.

## Running Tests

### Manual Execution
To run a specific test suite manually:
1. Navigate to the test directory:
   ```bash
   cd host_test/espnow_storage
   ```
2. Set the target to linux:
   ```bash
   idf.py --preview set-target linux
   ```
3. Build and run:
   ```bash
   idf.py build
   ./build/espnow_storage_host_test.elf
   ```

### Automated Execution
We use `pytest` to discover and run all host tests automatically.
- Run all tests:
  ```bash
  pytest
  ```
- Run with full logs:
  ```bash
  pytest -s
  ```
- Run a clean build from scratch (cleans all build folders):
  ```bash
  CLEAN_BUILD=1 python3 -m pytest -s test_host_automation.py
  ```

## Coverage Reports
You can generate a unified coverage report for all host tests.
1. Run the tests first to generate execution data.
2. Run the coverage script:
   ```bash
   python3 generate_coverage.py
   ```
The results will be available in:
- HTML Report: `host_test/coverage/html/index.html`
- Text Summary: `host_test/coverage/summary.txt`
