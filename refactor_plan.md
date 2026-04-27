# Encapsulating espnow_manager in a Namespace

This document outlines the proposed refactoring plan to wrap the `espnow_manager` component within its own namespace. This will prevent naming conflicts with other components (like `common`, `core`, etc.) in larger ESP-IDF applications.

## User Review Required

> [!IMPORTANT]  
> **Namespace Name:** I propose using `namespace espnow` as it is concise and aligns well with the domain.
> 
> **Version Bump:** This is a breaking change for downstream consumers. The component version in `idf_component.yml` should be incremented to 1.1.0.

## Proposed Changes

---

### Include Headers (`include/` and `include/interfaces/`)
All public and internal declarations will be wrapped in the chosen namespace.

#### [MODIFY] All `.hpp` files
- Add the namespace wrapper after the `#pragma once` and standard includes, but before class/struct definitions.
- Example:
  ```cpp
  #pragma once
  #include <cstdint>
  // ... other includes

  namespace espnow { // <-- Added

  class EspnowManager {
      // ...
  };

  } // namespace espnow <-- Added
  ```

---

### Source Files (`src/`)
All component implementations will be updated to reflect the namespace.

#### [MODIFY] All `.cpp` files
- Wrap implementations in the namespace block instead of using `using namespace` to ensure strict scoping.
- Ensure any `ESP_LOG` tags remain accessible (typically defined as static globals inside the file or anonymous namespace).

---

### Host Tests (`host_test/`)
The test suite needs to be adapted to test the namespaced classes.

#### [MODIFY] All `test_*.cpp` files
- Add `using namespace espnow;` at the top of the test files after the includes, OR explicitly prefix types (e.g., `espnow::EspnowManager`) to validate the API experience.
- Update any mock definitions to correctly mock the namespaced interfaces.

---

### Test Applications (`test_apps/`)
ESP-IDF applications used for real hardware testing need to be updated to consume the new API.

#### [MODIFY] `test_apps/**/main/*.cpp` files
- Update `app_main()` and other integration points to correctly instantiate `espnow::` objects and structures.

---

### Documentation & Build System
Update user-facing documents and component metadata.

#### [MODIFY] `README.md`, `API.md`, `DESIGN.md`
- Update all code examples and architectural descriptions to show the new namespace.
- Highlight the namespace change prominently.

#### [MODIFY] `CHANGELOG.md`
- Add an entry for the version release detailing the namespace addition and any breaking changes.

#### [MODIFY] `idf_component.yml`
- Increment the component version.

## Template Handling Considerations

### Template Specializations
The codebase contains numerous template definitions with SFINAE (`std::enable_if_t`) that require special handling when namespacing:

#### [MODIFY] Template Files (`include/protocol_types.hpp`, interface files)
- **Move explicit template instantiations** outside namespace blocks to avoid compilation issues
- **Update template function signatures** to use fully qualified names when referencing namespaced types
- **Template metaprogramming constructs** (SFINAE, concepts) must be tested thoroughly

#### Example Template Handling:
```cpp
namespace espnow {

template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
void some_function(T value);  // Template inside namespace

} // namespace espnow

// Explicit instantiation outside namespace (if needed)
template void espnow::some_function<NodeId>(NodeId value);
```

### Interface Namespace Strategy
#### Chosen Approach: Namespace all interfaces and implementations (Option B)
- **Pros**: Complete encapsulation, completely eliminates naming collisions with other ESP-IDF components.
- **Cons**: Requires explicit forward declarations (`namespace espnow { class IWiFiHAL; }`).
- **Implementation**: Wrap all interface classes (`IWiFiHAL`, `ITimerHAL`, etc.) and implementation classes inside the `espnow` namespace.

## Performance Considerations
- **Namespace overhead**: ZERO. Namespaces in C++ are a compile-time only construct for name mangling. There is NO runtime penalty and NO binary size increase.
- **Template instantiation**: SFINAE and explicit template instantiations work perfectly fine in C++17 within namespaces.
- **Debugging**: Namespaced symbols are fully supported by GDB/OpenOCD.

## Verification Plan

### Automated Tests
- **Host Tests**: Run the complete suite from the `host_test/` directory.
  - `cd host_test && ./run-tests.sh` (or `cmake` commands directly)
  - Ensure 100% pass rate and verify no coverage regressions.
- **Template Compilation Tests**: Verify all template instantiations compile correctly
- **Cross-Interface Tests**: Ensure interfaces work correctly with namespaced implementations

### Manual Verification / Integration Builds
- **Test Apps**: Perform a test build of all ESP-IDF projects inside `test_apps/`.
  - Navigate to each folder (e.g., `test_apps/test_multiple_node`)
  - Run `. $HOME/esp/esp-idf/export.sh && idf.py build`
  - This ensures the component compiles correctly in the cross-compilation environment and the API changes are valid.
- **Performance Testing**: Benchmark key operations to ensure no regressions

### ABI Compatibility Note
- ESP-IDF components are statically linked into the final firmware binary.
- Therefore, dynamic linking ABI compatibility is not a concern for this embedded environment.
