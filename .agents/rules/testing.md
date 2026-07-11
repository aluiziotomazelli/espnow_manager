---
trigger: always_on
---

# Testing Strategy

This reference defines the testing philosophy and technical implementation for the project. Host-based testing is the **primary** validation method.

## Host-Based Testing (`host_test/`)

Every component MUST be testable on the host machine (Linux) to ensure fast feedback and a robust development cycle.

### Execution Model
- **Target**: Linux (x86/x64).
- **Primary Tool**: GoogleTest (GTest) and GoogleMock (GMock).
- **Execution**:
    - **Individual**: Run within the specific test folder (e.g., `host_test/test_<subclass>/`).
    - **Full Suite**: Use `ctest` or a top-level runner from the `host_test/` directory.

### Project Layout
Each test project is a standalone IDF-style project:
1.  **CMakeLists.txt**: Project-level configuration.
2.  **main/CMakeLists.txt**: Component-under-test configuration.
3.  **main/main.cpp**: Entry point (runner).
4.  **main/test_<component>.cpp**: Test fixtures and cases.

---

## Mocking Strategy

### The Role of Mocks
Mocks are used to isolate the business logic from its external dependencies (HALs, other Managers).

### Mocking vs. Real Primitives
- **Mocks**: Primary choice for isolating logic from hardware (WiFi, NVS, Timer).
- **Real Primitives (`RealFreeRTOSHAL`)**: Use when the test needs to validate real task behavior, race conditions, or complex FreeRTOS interactions (e.g., `EspNowManager` task-based integration). This allows validating asynchronous logic on the host that is unreachable through pure mocks.
- **Shared Mocks Location**: `host_test_common/mock_hal_<subsystem>.hpp`.

---

## Testing Lifecycle & Maturity

To maintain a fast and sustainable development pace, follow this maturity-driven approach:

### Phase 1: Behavioral & TDD (Creation/Early Development)
- **Focus**: Validate expected behaviors and public API contracts.
- **Goal**: Enable aggressive refactoring and structural improvements without breaking the "what" of the component.
- **Avoid**: Do not aim for 100% branch/line coverage at this stage. High coverage on immature code creates "brittle" tests that require constant refactoring whenever the internal implementation changes.

### Phase 2: Deep Validation (Mature Code)
- **Focus**: Exhaustive branch testing, error-path injection, and high code coverage.
- **Goal**: Ensure robustness and catch edge cases once the architecture and internal logic have stabilized.
- **Transition**: Move to this phase only after the component's internal structure is mature.

---

## On-Target Testing (`test_apps/`)

On-target tests (ESP32) are **secondary** and used for integration and hardware validation.

### Types of Tests
- **Build Test**: `test_build/` ensures the component compiles for the real chip and initializes without crashing.
- **Functional Tests**: Validates behavior that cannot be mocked (e.g., real WiFi performance, NVS persistence).
- **Hardware Validation**: Ensures HAL implementations correctly wrap the underlying ESP-IDF APIs.

---

## Development Cycle (Test-Driven)

1.  **Define Interface**: Create/Update `include/interfaces/i_hal_<subsystem>.hpp`.
2.  **Create Mock**: Implement/Update `host_test_common/mock_hal_<subsystem>.hpp`.
3.  **Write Test Case**: Create/Update test in `host_test/test_<component>/`.
4.  **Implement Logic**: Write code in `src/` until the test passes.
5.  **Target Build**: Run `test_apps/test_build` to ensure ESP32 compatibility.

---

## Continuous Validation

- **No Regressions**: Every bug fix MUST be accompanied by a new test case that reproduces the failure.
- **Code Coverage**: Aim for high coverage of business logic in host tests.
- **Automation**: Ensure all tests are compatible with CTest for automated execution.
