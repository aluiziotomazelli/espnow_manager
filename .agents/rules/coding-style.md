---
trigger: always_on
---

# Coding Standards & Style Guide

This reference defines the coding conventions for the project. Adherence to these standards is mandatory for all new code and refactorings.

## Naming Conventions

### General Rules
- **Language**: All names MUST be in English.
- **Characters**: ASCII only. No abbreviations unless industry standard (e.g., NVS, WiFi, HAL).

### Symbols
| Entity | Case | Example |
| :--- | :--- | :--- |
| **Classes** | `PascalCase` | `WifiManager`, `LedController` |
| **Interfaces** | `IPascalCase` | `IHalWifi`, `ILedDriver` |
| **Methods / Functions** | `snake_case` | `init()`, `read_value()` |
| **Variables** | `snake_case` | `retry_count`, `current_state` |
| **Private Members** | `snake_case_` | `state_`, `buffer_` (trailing underscore) |
| **Constants / Macros** | `UPPER_CASE` | `MAX_RETRY`, `DEFAULT_TIMEOUT` |
| **Namespaces** | `snake_case` | `wifi_manager` |
| **Enums** | `PascalCase` | `enum class SystemState { IDLE, RUNNING }` |

### Files
- **Standard Files**: `snake_case.cpp` / `snake_case.hpp`.
- **Interface Files**: `i_<name>.hpp` (e.g., `i_hal_wifi.hpp`).
- **HAL Implementation**: `hal_<subsystem>.cpp` / `hal_<subsystem>.hpp`.

---

## File Structure & Headers

Every file MUST start with the relative path and a `#pragma once` guard.

```cpp
// relative/path/to/file.ext
#pragma once

#include <cstdint>      // Standard library

#include <gtest/gtest.h> // Third-party

#include "esp_wifi.h"    // ESP-IDF Headers
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include "esp_log.h"

#include "i_hal_wifi.hpp" // Project: Interfaces first
#include "wifi_manager.hpp" // Project: Implementations

static const char *TAG = "WifiManager"; // Static TAG for logging
```

### Include Ordering Rules:
1.  **Standard Library**: `<brackets>`
2.  **Third-party Libraries**: (e.g., GoogleTest)
3.  **ESP-IDF Components**: Native Espressif headers.
4.  **Project Headers**:
    -   Interfaces first.
    -   Concrete classes second.
-   **Note**: Use a single blank line between each group.

---

## Logging & Output

- **Production Code**: Use the ESP-IDF logging framework (`ESP_LOGI`, `ESP_LOGE`, etc.).
- **Host-Based Tests**: Use `printf` only for debugging output.
- **Local Level**: Always `#define LOG_LOCAL_LEVEL` before including `esp_log.h`.

---

## Error Handling

### ESP-IDF Related Code
- Functions interacting with hardware or ESP-IDF APIs MUST return `esp_err_t` if it's the natural return type.
- For FreeRTOS primitives, use their native return types (e.g., `BaseType_t`, `pdPASS`).
- **Check Returns**: Callers MUST verify the return value against `ESP_OK`.

### Pure Logic & State Machines
- Use `bool` for simple success/fail.
- Use a custom `enum` or `enum class` when multiple return states are possible (e.g., FSM state queries).

### Constraints
- **Exceptions are strictly forbidden**.
- **No Emojis**: Do not use emojis in log messages or comments.
