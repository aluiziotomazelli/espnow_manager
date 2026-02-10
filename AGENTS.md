# Agent Guidelines for This Project

Hello Agent! Welcome to the project. Please follow these guidelines to ensure your contributions are consistent with the existing codebase.

## 1. General Philosophy & Architecture

- **C++ and Object-Oriented:** The project is written in C++ on top of the ESP-IDF framework. New functionality should be encapsulated within classes.
- **Non-Blocking Operations:** Components that perform long-running or asynchronous operations (like communication or complex sensor readings) **MUST** use a dedicated FreeRTOS task. The public API should be non-blocking, typically posting messages to a private queue for the task to process. Avoid blocking `app_main` or other critical tasks.

## 2. Code Style and Conventions

- **Naming Conventions:**
    - **Functions/Methods:** `snake_case()`
    - **Variables:** `snake_case`
    - **Private Class Members:** `snake_case` suffixed with an underscore (e.g., `task_handle_`, `config_`).
    - **Filenames:** `snake_case` (e.g., `water_tank_nvs.cpp`).

- **Header Guards:**
    - Always use `#pragma once` as the header guard for all `.h` or `.hpp` files.

- **Include Ordering & File Structure:**
    Follow this exact sequence, with a single blank line separating the groups:
    1. **Header Guard:** `#pragma once`
    2. **Standard Libraries:** C/C++ standard headers using angle brackets (e.g., `#include <cstdint>`).
    3. **ESP-IDF Components:** Native Espressif components (e.g., `#include "driver/gpio.h"`).
    4. **Logging Setup:** ```cpp
       #define LOG_LOCAL_LEVEL ESP_LOG_INFO
       #include "esp_log.h"
       ```
    5. **Project Headers:** Local headers and custom components using double quotes.

- **Comments and Documentation:**
    - All comments, documentation, and commit messages **MUST** be in **English**.
    - Header files (`.hpp`) should use Doxygen-style comments for public APIs.
    - Implementation files (`.cpp`) can use simpler `//` comments for clarification.

- **Logging:**
    - Use the ESP-IDF logging framework (`ESP_LOGI`, `ESP_LOGE`, etc.).
    - To ensure proper log level filtering, `#define LOG_LOCAL_LEVEL` must be declared before including `esp_log.h`. Default level: `ESP_LOG_INFO`.
    - Define a static `TAG` constant at the beginning of each `.cpp` file for consistent log messages.

## 3. Error Handling

- Functions that can fail (e.g., hardware interaction, memory allocation) **MUST** return an `esp_err_t`.
- Callers **MUST** check the return value of these functions against `ESP_OK` and handle any potential failures gracefully. Do not ignore error codes.

## 4. Build System

- The project uses CMake and the ESP-IDF build system.

- **Pre-Build Requirements:**
    1. **Directory Navigation**: Before any build, you MUST `cd` into the specific application folder (e.g., `cd test_apps/test_*`).
    2. **Environment Activation**: The ESP-IDF environment must be sourced within the same shell execution: `. $HOME/esp/esp-idf/export.sh`, the `IDF_PATH` is also configured to `/home/jules/esp/esp-idf`.

- **Standard Build Procedure:**
    - **Targeting:** Identify the chip via `sdkconfig.defaults.*` (e.g., `sdkconfig.defaults.esp32c3`). If no `sdkconfig.defaults.*` exists, use `esp32`as default with 4M of flash. Run `idf.py set-target <target>` if the build fails due to wrong architecture.
    - **Cleaning:** Use `idf.py fullclean`. If it fails, manually run `rm -rf build/`.
    - **Building:** Chain the commands to ensure path persistence:
      `cd <app_folder> && . $HOME/esp/esp-idf/export.sh && idf.py build`
      
## 6. Technical Reference & Documentation

- **Official Docs:** Whenever interacting with ESP-IDF Components, consult the documentation, e.g.: [Latest Stable ESP-IDF API Reference](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/index.html) if the target if the target is `esp32`. Remember to consult the app's target (when applicable) and refer to its documentation. Some chips do not support multicore, while others have hardware-level functions that are slightly different.
- **FreeRTOS (IDF Version):** When handling tasks, queues, or semaphores, specifically consult the **FreeRTOS (IDF)** section of the API Reference. Be aware that ESP-IDF's FreeRTOS has specific differences and optimizations (such as multicore support/affinity) compared to "Vanilla" FreeRTOS. 
- **Up-to-Date APIs:** Avoid using deprecated functions. Always prefer the modern, recommended driver or component APIs as per the documentation for version v5.1.1.      

