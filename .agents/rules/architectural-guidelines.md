---
trigger: always_on
---

# Architectural Guidelines

This reference defines the core architectural principles of the project: Component-based design, strict Hardware Abstraction, and Testability.

## Component-Oriented Design

- **Preference**: Always prefer creating or extending specific components over modifying `main` or `app_main`.
- **Boundaries**: Maintain clear, decoupled boundaries between components to ensure independent evolution and testing.

## SRP (Single Responsibility Principle)

- **One Responsibility**: Each class MUST have a single, well-defined reason to change.
- **Avoid God Classes**: Large classes that manage multiple subsystems are strictly forbidden. Break them down into smaller, focused managers.

## Hardware Abstraction Layer (HAL)

### The Strict Rule
**No direct ESP-IDF or FreeRTOS C API calls are allowed outside the HAL implementation files.**

Business logic must never depend on hardware-specific headers (e.g., `esp_wifi.h`, `driver/gpio.h`). All interactions with the underlying OS or hardware must be abstracted.

### Subsystem Structure & Granularity
For every hardware/OS subsystem, the structure is:
1.  **Interface**: `include/interfaces/i_hal_<subsystem>.hpp` (Abstract Base Class).
2.  **Implementation**:
    -   `include/hal_<subsystem>.hpp` (Header).
    -   `src/hal_<subsystem>.cpp` (Implementation: thin wrapper over ESP-IDF).
3.  **Mock**: `host_test_common/mock_hal_<subsystem>.hpp` (For unit testing).

### Strict HAL Design Rules
- **Single Header Rule**: Each HAL MUST cover only one ESP-IDF or FreeRTOS header (e.g., do not mix WiFi and FreeRTOS calls in the same HAL).
- **1:1 Mapping**: Every HAL function MUST be an exact 1:1 wrapper of the original API.
- **No Logic**: HAL methods MUST NOT contain any internal logic, branching, or state. They are strictly pass-through.
- **Signature Integrity**: Use the exact same parameter types and return types as the original ESP-IDF/FreeRTOS functions.

## Dependency Injection (DI)

- **Interfaces First**: All external dependencies (HALs, other Managers) must be accessed via interfaces defined in `include/interfaces/`.
- **Interface Documentation**: All interface methods MUST include a `@copydoc` reference to the original function for clear traceability.
  *Example*:
  ```cpp
  /** @copydoc esp_timer_get_time() */
  virtual uint64_t get_time_us() const = 0;
  ```
- **Constructor Injection**: Dependencies MUST be injected via the constructor. This ensures that the class is always testable and its dependencies are explicit.
- **No Global State**: Avoid singletons or global variables. The component lifecycle and dependencies should be managed by the application or a factory.

## Testing as a First-Class Citizen

- **Host-First**: Design every component to be testable on the host (Linux) from the start.
- **Mocks**: Use Google Mock and the shared mocks in `host_test_common/` to isolate the component under test from its hardware dependencies.
