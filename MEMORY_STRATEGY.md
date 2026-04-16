# Memory Allocation Strategy: Static vs. Hybrid

This document analyzes two proposals for managing the lifecycle and allocation of the `EspNowManager` and its dependencies, balancing the constraints of embedded production environments (ESP32) with the requirements of host-based unit testing.

---

## Proposal 1: Pure Local Static (Zero-Heap Production)

In this model, `instance()` manages all dependencies as local static objects. The manager uses a private constructor that accepts references or raw pointers to these objects.

### Implementation Sketch

```cpp
// EspNowManager.cpp
EspNowManager& EspNowManager::instance() {
    static WiFiHAL hal_wifi;
    static MessageCodec codec;
    static DiscoveryManager scanner(hal_wifi, codec, ...);
    // ...
    static EspNowManager inst(hal_wifi, codec, scanner, ...);
    return inst;
}
```

### Trade-offs

| Feature | Impact |
| :--- | :--- |
| **Heap Usage** | **Zero**. All managers are allocated in the `.bss` segment at compile time. |
| **Complexity** | **Very Low**. No `unique_ptr` or ownership flags needed. |
| **Safety** | **High (Production)**. Objects are guaranteed to live for the duration of the app. |
| **Testability** | **Poor**. In host-based tests (GTest), static objects persist across tests. State leakage occurs, and replacing a `static WiFiHAL` with a `MockWiFiHAL` is impossible without global pointer hacks. |

---

## Proposal 2: Hybrid Conditional Ownership (Reference + unique_ptr)

The manager stores dependencies as raw pointers and tracks ownership via a boolean flag. It provides two constructors: one for production (references) and one for testing (`unique_ptr`).

### Implementation Sketch

```cpp
class EspNowManager {
public:
    // Production: Zero-heap, caller ensures lifetime (static objects)
    EspNowManager(IWiFiHAL& wifi, ...) : hal_wifi_(&wifi), owns_deps_(false) {}

    // Testing: Takes ownership, cleans up in destructor
    EspNowManager(std::unique_ptr<IWiFiHAL> wifi, ...) 
        : hal_wifi_(wifi.release()), owns_deps_(true) {}

    ~EspNowManager() {
        if (owns_deps_) {
            delete hal_wifi_;
            // ...
        }
    }
private:
    IWiFiHAL* hal_wifi_;
    bool owns_deps_;
};
```

### Trade-offs

| Feature | Impact |
| :--- | :--- |
| **Heap Usage** | **Zero (Production)**. Uses the reference constructor with static objects. |
| **Complexity** | **Medium**. Requires manual deletion in the destructor and double constructors. |
| **Safety** | **Medium**. Requires careful implementation of the destructor to avoid leaks or double-frees. |
| **Testability** | **Excellent**. Tests use the `unique_ptr` constructor to inject mocks. Each test creates and destroys its own instance, ensuring a clean slate. |

---

## Comparison Summary

| Criteria | Proposal 1 (Pure Static) | Proposal 2 (Hybrid) |
| :--- | :--- | :--- |
| **Production Heap** | 0 bytes | 0 bytes |
| **Mocking Support** | Difficult / Global state | Native / Clean |
| **State Leakage** | High (Test A affects Test B) | None (Destructor cleans up) |
| **Idiomatic Quality** | Traditional Embedded C++ | Modern Dependency Injection |

## Recommendation

**Proposal 2 (Hybrid)** is recommended for this project. 

It satisfies the **ESP-NOW Manager** mandate of 100% host-based testability while fulfilling the production requirement of zero heap fragmentation. It allows the `instance()` factory to remain zero-heap while providing the flexibility needed for GoogleMock to inject behaviors during testing.
