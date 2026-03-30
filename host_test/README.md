# ESP-NOW Manager Host Tests

This directory contains host-based tests for the ESP-NOW Manager component. Host testing allows you to run unit tests on your development machine (Linux) instead of the target microcontroller, enabling faster development cycles and the use of advanced mocking frameworks like Google Mock.

## Test Scope

The tests cover individual components (Unit Tests) and their interactions (Integration Tests). We aim for high coverage across all critical communication logic, including:

- Protocol encoding/decoding and CRC validation (`test_message_codec/`)
- Peer management and persistent storage (`test_peer_manager/`, `test_storage_manager/`)
- Transmission state machine and retry logic (`test_tx_state_machine/`, `test_tx_manager/`)
- Heartbeat monitoring and node health tracking (`test_heartbeat_manager/`)
- Pairing process and channel discovery (`test_pairing_manager/`, `test_discovery_manager/`)
- Message routing and dispatching (`test_message_router/`)
- Node state machine governance (`test_node_state_machine/`)
- Channel monitoring (`test_channel_monitor/`)
- ESP-NOW driver initialization (`test_espnow_driver/`)
- Full EspNowManager integration (`test_espnow_manager/`)

## Mocks and Abstractions

To isolate the logic under test, we use several mocking strategies:

### Google Mock (GMock)

All component interfaces are mocked using Google Mock, providing:
- **Call Spying:** Track if methods were called and with what arguments
- **Stubbing:** Configure return values and behaviors for specific test scenarios
- **State Simulation:** Simulate internal state changes (like task handles or queue statuses)

Mock files are located in `host_test/common/`:
- `mock_hal_wifi.hpp`, `mock_hal_espnow.hpp` - WiFi and ESP-NOW HALs
- `mock_hal_freertos.hpp`, `mock_hal_timer.hpp` - FreeRTOS and Timer HALs
- `mock_peer_manager.hpp`, `mock_tx_manager.hpp` - Component managers
- `mock_message_codec.hpp`, `mock_message_router.hpp` - Protocol components
- And others...

### ESP-IDF Host Testing

We leverage the ESP-IDF host testing framework to mock standard ESP-IDF components like `esp_wifi` and `esp_now`. These mocks simulate the behavior of the ESP32 hardware and drivers on Linux.

## Directory Structure

```
host_test/
├── common/                     # Shared mocks and utilities
│   ├── mock_hal_*.hpp
│   ├── mock_*.hpp
│   └── ...
├── test_channel_monitor/       # ChannelMonitor tests
├── test_discovery_manager/     # DiscoveryManager tests
├── test_espnow_driver/         # EspNowDriver tests
├── test_espnow_manager/        # Full EspNowManager integration tests
├── test_heartbeat_manager/     # HeartbeatManager tests
├── test_message_codec/         # MessageCodec tests
├── test_message_router/        # MessageRouter tests
├── test_node_state_machine/    # NodeStateMachine tests
├── test_pairing_manager/       # PairingManager tests
├── test_peer_manager/          # PeerManager tests
├── test_storage_manager/       # StorageManager tests
├── test_tx_manager/            # TxManager tests
├── test_tx_state_machine/      # TxStateMachine tests
├── CMakeLists.txt              # Unified test configuration
├── coverage_common.cmake       # Shared coverage logic
└── README.md                   # This file
```

## Running Tests

### Prerequisites

- Linux host machine
- ESP-IDF environment set up and sourced
- `libbsd` and `libbsd-dev` packages (for Linux target)
- `lcov` and `genhtml` (for coverage reporting)

### Running Individual Tests

To run a specific test suite:

1. Navigate to the test directory:
   ```bash
   cd host_test/test_peer_manager
   ```

2. Set the target to Linux (usually not needed):
   ```bash
   idf.py set-target linux
   ```
   > **Note:** Each test directory includes a `sdkconfig.defaults` file that automatically configures the target as `linux`. You only need to run this command if the build fails with a target mismatch error.

3. Build the test:
   ```bash
   idf.py build
   ```

4. Run the executable:
   ```bash
   ./build/test_peer_manager.elf
   ```

> **Logging in Tests:** Avoid using `ESP_LOGx` macros in host tests. If logging is needed, use `printf(...)` instead, as ESP-IDF logging is disabled by default in `sdkconfig.defaults`.

### Running All Tests (Unified)

To run all tests and generate a unified coverage report:

1. **Configure the project**:
   ```bash
   cd host_test
   mkdir -p build && cd build
   cmake ..
   ```
2. Build all tests
   ```bash
   make build_all_tests
   ```
3. Clear coverage previous files
   ```bash
   make coverage_clean
   ``` 
4. Run all tests
   ```bash
   ctest --output-on-failure
   ``` 
5. Generate coverage report
   ```bash
   make unified_coverage
   ```

- Alternatively, run steps 2, 3, 4 and 5 with the following command:
   ```bash
   make run_all_tests
   ```
*Note: If you use Ninja, replace `make` with `ninja`.*

The report will be available at `host_test/coverage/index.html`.


## Generating Coverage

### Individual Test Coverage

After running a test, generate coverage for that specific test:

```bash
cd host_test/test_peer_manager/build
idf.py generate_coverage
```

The report will be in `host_test/test_peer_manager/coverage/`.

### Unified Coverage (All Tests)

The unified coverage report aggregates data from all test suites:

```bash
cd host_test/build
make run_all_tests
```

The report is available at `host_test/coverage/index.html`.

## CI/CD Integration

This project includes a GitHub Actions workflow (`.github/workflows/host_test.yml`) that automatically:

- Installs necessary dependencies (`lcov`)
- Builds and runs all host tests on every push/PR to `main`
- Generates the unified coverage report
- Deploys the coverage report to GitHub Pages (only on pushes to `main`)

## Shared Coverage Logic

The coverage logic is centralized in `host_test/coverage_common.cmake`. Individual test projects include this file to maintain consistency and reduce duplication.

## Test List

For a complete list of tests and their coverage, see [test_list.md](test_list.md).
