# ESP-NOW Manager

## Project Overview

**espnow_manager** is an ESP-IDF component written in C++17 that provides a robust, high-level abstraction for the ESP-NOW protocol. It features peer discovery, pairing, reliable message transmission (with retries and state machines), heartbeat monitoring, and persistent storage of peer information.

## Architecture

The project follows a **Facade + Decentralized Managers** pattern, adhering to the Single Responsibility Principle (SRP).

*   **Facade**: `EspNowManager` is the single public entry point. It orchestrates sub-components but delegates actual logic.
*   **Managers**:
    *   `TxManager`: Handles packet queuing, retries, and transmission state (READY/SCANNING).
    *   `DiscoveryManager`: Scans WiFi channels to find peers.
    *   `HeartbeatManager`: Monitors link health via periodic heartbeats.
    *   `PairingManager`: Manages secure enrollment of new peers.
    *   `PeerManager` & `StorageManager`: Maintains the peer database with dual-layer persistence (RTC RAM for fast wake-up + NVS for long-term storage).
*   **HAL Abstraction**: All hardware dependencies (WiFi, FreeRTOS, NVS, Timer) are abstracted via interfaces (`include/interfaces/`). This enables 100% host-based testing with mocks.

## Directory Structure

*   `src/`: Implementation of managers and logic.
*   `include/`: Public API headers.
    *   `interfaces/`: Abstract base classes for dependency injection.
*   `host_test/`: Host-based unit tests (running on Linux).
    *   Each manager has its own test project (e.g., `test_discovery_manager`).
    *   `gtest/`: GoogleTest integration.
*   `legacy_host_test/`: Deprecated Python-based tests.

## File Includes and Guards

* `// relative_path/file_name.ext`
* `#pragma once`
blank_line
* `#include <global1>`: like std and c++ libs, or gtest
* `#include <global2>`: like std and c++ libs, or gtest
blank_line
* `#include "esp_idf_component1.h"`: internal headers from ESP_IDF
* `#include "esp_idf_component2.h"`: internal headers from ESP_IDF
blank_line
* `#include "i_interface1.hpp"`: headers from project
* `#include "my_class1.hpp"`: headers from project

## ESP-NOW Component — Coding Preferences

- **Language:** C++ / ESP-IDF; all code, comments, and naming strictly in English
- **Architecture:** HAL abstraction via injected interfaces (e.g. `INvsHAL`, `IFreeRTOSHAL`); no direct ESP-IDF calls in business logic
- **Dependencies:** Always injected via constructor; no global state
- **State mutations:** Only after confirming HAL/operation success (e.g. don't update local list if `hal_del_peer` fails)
- **Resource management:** RAII for mutexes (`FreeRTOSMutexGuard` pattern)
- **Eviction logic:** Prefer `std::min_element` over positional assumptions like `back()`
- **Arithmetic:** Integer over float for simple multipliers (avoids FP overhead on embedded)
- **Memory:** Prefer static allocation; accept dynamic (e.g. `std::vector`) when convenience and rarely used
  justifies the cost, always pairing with `.reserve()` to avoid heap fragmentation
- **TODOs:** Kept as living documentation; intentional, not noise
- **Commits:** Conventional commits format
- **Testing:** Host-side GTest/GMock; separate mock header files; `NiceMock` for focused tests
- **Guidance style:** Explain the reasoning behind patterns, not just the solution

## Build & Test Instructions
The build process, tests and coverage report must be executeds **only when explicitly** requested by user.

### 1. Target Build (ESP32)

Standard ESP-IDF build process:

```bash
cd cd test_apps/build_test
. $HOME/dev/esp/esp-idf/export.sh
idf.py build
```

### 2. Host Tests (Linux)

The project uses ESP-IDF's Linux target support for running unit tests on the host machine. Tests are located in `host_test/` and are structured as separate IDF projects.

**Prerequisites:**
*   ESP-IDF environment set up.
*   Linux environment (or WSL).
*   `lcov` (for coverage).

**Running a Test (e.g., DiscoveryManager):**

```bash
cd host_test/test_discovery_manager
idf.py set-target linux
idf.py build
./build/test_discovery_manager.elf
```

**Generating Coverage Reports:**

Each test project has a custom `generate_coverage` target defined in `coverage_common.cmake`.

```bash
cd host_test/test_discovery_manager
# Ensure the project is built first
idf.py build
# Run the coverage target (using ninja directly is most reliable)
ninja -C build generate_coverage
# Report is generated in: host_test/test_discovery_manager/coverage/index.html
```
## Development Conventions

*   **Language**: C++17.
*   **Style**: Google C++ Style (enforced via `.clang-format`).
*   **Dependency Injection**:
    *   **Strict Rule**: Never call FreeRTOS or ESP-IDF C APIs directly in business logic.
    *   **Pattern**: Inject `IWiFiHAL`, `IFreeRTOSHAL`, etc., into manager constructors.
    *   **Reason**: Enables mocking for host tests.
*   **Testing**:
    *   New features MUST have host-based unit tests (`host_test/`).
    *   Use `GoogleMock` to mock dependencies.
*   **Naming**:
    *   Files: `snake_case.cpp`; interfaces: `i_name`; HALs: `hal_name`; mock: `mock_name`
    *   Classes: `PascalCase`
    *   Methods/Variables: `snake_case`
    *   Private class members: `terminal_underscore_` 
    *   Interfaces: `I` prefix (e.g., `IDiscoveryManager`).

## Key Workflows

*   **Adding a Feature**:
    1.  Define/Update Interface in `include/interfaces/`.
    2.  Update/Create Mock in `host_test/common/`.
    3.  Write Test Case in `host_test/test_<feature>/main/`.
    4.  Implement Logic in `src/`.
    5.  Run Test & Coverage.

*   **Modifying State Machine**:
    *   `TxStateMachine` (in `src/tx_state_machine.cpp`) controls transmission states.
    *   Transitions are triggered by `TxManager` events (ACK, NO_ACK).
