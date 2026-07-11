---
trigger: always_on
---

# Project Structure

This reference provides a map of the codebase and the rules for organizing files and components.

## Directory Map

| Directory | Purpose |
| :--- | :--- |
| `src/` | Component implementation files (`.cpp`). |
| `include/` | Public API headers (`.hpp`). |
| `include/interfaces/` | Abstract base classes (Interfaces) for dependency injection. |
| `host_test/` | Host-based unit test projects (Target: Linux). |
| `host_test/gtest/` | GoogleTest wrapper and setup. **Do not modify.** |
| `host_test_common/` | Shared mocks and test utilities used across projects. |
| `test_apps/` | On-target integration and build tests (Target: ESP32). |
| `external/` | Third-party dependencies and external libraries. |
| `legacy_*/` | Deprecated code, tests, or scripts. |

---

## Organization Rules

### Source and Headers
- **Implementation**: All `.cpp` files MUST reside in `src/`.
- **Public API**: Headers required by other components MUST reside in `include/`.
- **Private Headers**: Headers used only within a single component SHOULD reside in `src/`.

### Interfaces and HALs
- All interfaces (prefixed with `i_`) MUST be placed in `include/interfaces/`.
- HAL implementations (thin wrappers) MUST follow the `hal_<subsystem>` naming and be split between `include/` and `src/`.

### Testing Layout
- **Unit Tests**: Every major component SHOULD have a corresponding project in `host_test/test_<component_name>/`.
- **Shared Mocks**: Always check `host_test_common/` before creating a new mock. If a mock for a HAL is missing, create it there for global use.

---

## Legacy Code (Strict Rule)

**The `legacy_*` directories are strictly for reference only.**
- **Never Modify**: Do not change any file within a `legacy_` folder.
- **Never Import**: Do not include headers from legacy folders in new code.
- **Ignore by Default**: Act as if these folders do not exist unless explicitly asked by the user to perform a migration.
