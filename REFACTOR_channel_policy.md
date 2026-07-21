# Plano de Refatoração: `ChannelPolicy` + Remoção de `wifi_set_channel()` do Init

> Escopo: resolver o travamento do `EspNowManager` quando o nó está conectado a um AP e o `EspNowDriver` ou `DiscoveryManager` tenta setar o canal. O resto (`store_channel`, `wifi_channel` em `EspNowConfig`) fica para refatoração futura documentada em `DESIGN_NOTE_channel_ownership.md`.

---

## Problema a Resolver

1. **`EspNowDriver::init()` L45**: Chama `wifi_set_channel()` sempre, mesmo quando o nó está conectado a um AP. Falha silenciosamente ou causa desconexão.
2. **`DiscoveryManager::scan_channel()` L189**: Chama `wifi_set_channel()` em loop sobre os 13 canais. Se conectado, a chamada falha e o scan retorna `ESP_FAIL`, travando o `EspNowManager`.
3. Não há mecanismo para a aplicação informar ao `EspNowManager` que a varredura de canais não deve ser feita (nó conectado ao mesmo AP que o hub).

---

## Solução: `ChannelPolicy` via Push

A aplicação empurra a política via setter. Zero dependência nova no `espnow_manager`.

```
WIFI CONNECTED  → espnow_manager.set_channel_policy(ChannelPolicy::FIXED)
WIFI DISCONNECTED → espnow_manager.set_channel_policy(ChannelPolicy::SCAN)
```

---

## Arquivos Modificados

| Arquivo | Tipo de mudança |
|---|---|
| `include/espnow_types.hpp` | Adiciona enum `ChannelPolicy` |
| `include/interfaces/i_discovery_manager.hpp` | Adiciona `set_channel_policy()` |
| `include/discovery_manager.hpp` | Adiciona membro `policy_` + declaração |
| `src/discovery_manager.cpp` | Implementa guarda no `scan_channel()` |
| `include/interfaces/i_espnow_manager.hpp` | Adiciona `set_channel_policy()` à interface pública |
| `include/espnow_manager.hpp` | Adiciona declaração do método público |
| `src/espnow_manager.cpp` | Implementa delegação para `scanner_` |
| `src/espnow_driver.cpp` | Remove `wifi_set_channel()` do `init()` |

---

## Passo a Passo

### Passo 1 — Adicionar enum `ChannelPolicy` em `espnow_types.hpp`

```cpp
// Adicionar próximo aos outros enums de configuração

/**
 * @brief Controls whether the DiscoveryManager is allowed to change the WiFi
 *        channel when scanning for the hub.
 *
 * Use SCAN when the node is not connected to any AP (standalone ESP-NOW only).
 * Use FIXED when the node is connected to a WiFi AP: the channel is locked by
 * the AP association and cannot be changed; both the hub and this node are
 * assumed to be on the same AP channel.
 *
 * Default: SCAN (preserves original behavior).
 */
enum class ChannelPolicy : uint8_t {
    SCAN,   ///< Node may call wifi_set_channel() during discovery scan (standalone mode)
    FIXED,  ///< Node is WiFi-connected; channel is owned by the AP — no scanning allowed
};
```

---

### Passo 2 — Adicionar `set_channel_policy()` em `IDiscoveryManager`

```cpp
// Em i_discovery_manager.hpp, após set_channel():

/**
 * @brief Set the WiFi channel scanning policy.
 *
 * SCAN (default): scan_channel() will iterate over all 13 channels calling
 *   wifi_set_channel() until the hub responds.
 * FIXED: scan_channel() returns ESP_OK immediately without changing the
 *   channel. The caller is assumed to be on the same channel as the hub
 *   (both connected to the same AP).
 *
 * @param policy ChannelPolicy::SCAN or ChannelPolicy::FIXED
 * @note Thread-safe (stores atomically).
 */
virtual void set_channel_policy(ChannelPolicy policy) = 0;

/**
 * @brief Get the current channel scanning policy.
 */
virtual ChannelPolicy get_channel_policy() const = 0;
```

---

### Passo 3 — Implementar em `DiscoveryManager`

**`discovery_manager.hpp`** — adicionar membro em private:
```cpp
std::atomic<uint8_t> policy_ = static_cast<uint8_t>(ChannelPolicy::SCAN);
```

**`discovery_manager.hpp`** — declarar métodos:
```cpp
void set_channel_policy(ChannelPolicy policy) override;
ChannelPolicy get_channel_policy() const override;
```

**`discovery_manager.cpp`** — implementar:
```cpp
void DiscoveryManager::set_channel_policy(ChannelPolicy policy)
{
    policy_.store(static_cast<uint8_t>(policy));
    ESP_LOGI(TAG, "Channel policy set to %s",
             policy == ChannelPolicy::FIXED ? "FIXED (WiFi connected)" : "SCAN (standalone)");
}

ChannelPolicy DiscoveryManager::get_channel_policy() const
{
    return static_cast<ChannelPolicy>(policy_.load());
}
```

**`discovery_manager.cpp`** — guarda no início de `scan_channel()`:
```cpp
esp_err_t DiscoveryManager::scan_channel()
{
    // If the node is connected to a WiFi AP, channel switching is not allowed.
    // Both this node and the hub are assumed to be on the same AP channel.
    if (static_cast<ChannelPolicy>(policy_.load()) == ChannelPolicy::FIXED) {
        ESP_LOGI(TAG, "ChannelPolicy::FIXED — skipping channel scan, assuming shared AP channel.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting channel scan to find Hub.");
    // ... resto do código atual inalterado
```

---

### Passo 4 — Expor `set_channel_policy()` na `IEspNowManager` e `EspNowManager`

**`i_espnow_manager.hpp`** — na seção de lifecycle/configuration:
```cpp
/**
 * @brief Set the WiFi channel scanning policy.
 *
 * Call with ChannelPolicy::FIXED when the node connects to a WiFi AP so that
 * the discovery manager does not attempt to change the channel (which would
 * fail while connected and could disrupt connectivity).
 * Call with ChannelPolicy::SCAN when the node is not connected to any AP.
 *
 * Typical usage:
 * @code
 * wifi_manager.on_state_change([&](WifiState s) {
 *     bool connected = (s == WifiState::CONNECTED);
 *     espnow_manager.set_channel_policy(
 *         connected ? ChannelPolicy::FIXED : ChannelPolicy::SCAN);
 * });
 * @endcode
 *
 * @param policy ChannelPolicy::SCAN (default) or ChannelPolicy::FIXED
 * @note Thread-safe. Can be called before or after init().
 */
virtual void set_channel_policy(ChannelPolicy policy) = 0;
```

**`espnow_manager.hpp`** — declarar:
```cpp
void set_channel_policy(ChannelPolicy policy) override;
```

**`espnow_manager.cpp`** — implementar:
```cpp
void EspNowManager::set_channel_policy(ChannelPolicy policy)
{
    if (scanner_ != nullptr) {
        scanner_->set_channel_policy(policy);
    }
}
```

---

### Passo 5 — Remover `wifi_set_channel()` do `EspNowDriver::init()`

**`espnow_driver.cpp`** — remover o bloco:

```diff
-    err = wifi_hal_.wifi_set_channel(config.wifi_channel, WIFI_SECOND_CHAN_NONE);
-    if (err != ESP_OK) {
-        return init_fail(err, "Failed to set WiFi channel");
-    }
-
     err = add_broadcast_peer();
```

> O canal do `EspNowDriver` agora é irrelevante no init. Quando em `SCAN`, o `DiscoveryManager` seta o canal durante a varredura. Quando em `FIXED`, o canal é do AP e não deve ser alterado.

---

### Passo 6 — Uso na aplicação (`main.cpp` do water-tank)

```cpp
// Já existe o callback do wifi_manager. Adicionar nele:
wifi_manager.on_state_change([&](WifiState s) {
    bool connected = (s == WifiState::CONNECTED);
    espnow_manager.set_channel_policy(
        connected ? espnow::ChannelPolicy::FIXED : espnow::ChannelPolicy::SCAN);
});

// Ou, se o water-tank sempre roda conectado (modo de produção):
espnow_manager.set_channel_policy(espnow::ChannelPolicy::FIXED);
```

---

## Estratégia de Testes

### Testes existentes (`host_test/test_discovery_manager`)

Os testes de `scan_channel()` existentes cobrem o cenário `SCAN`. Adicionar:

```cpp
TEST(DiscoveryManagerTest, ScanChannelSkipsWhenPolicyFixed)
{
    // Arrange
    scanner->set_channel_policy(ChannelPolicy::FIXED);

    // Act — wifi_set_channel deve NUNCA ser chamado
    EXPECT_CALL(*mock_wifi, wifi_set_channel(_, _)).Times(0);
    esp_err_t result = scanner->scan_channel();

    // Assert
    EXPECT_EQ(ESP_OK, result);
}

TEST(DiscoveryManagerTest, ScanChannelRunsNormallyWhenPolicyScan)
{
    scanner->set_channel_policy(ChannelPolicy::SCAN);
    // testes já existentes passam sem modificação
}
```

### Mock do `IDiscoveryManager`

Adicionar ao mock existente:
```cpp
MOCK_METHOD(void, set_channel_policy, (ChannelPolicy), (override));
MOCK_METHOD(ChannelPolicy, get_channel_policy, (), (const, override));
```

---

## O Que Fica Fora do Escopo (ver `DESIGN_NOTE_channel_ownership.md`)

- Remover `wifi_channel` de `EspNowConfig`
- Remover `scanner_->set_channel()` do `EspNowManager::init()`
- Remover `storage_->store_channel()` e `load_channel()`
- Canal explícito vs `channel = 0` nos peers (já correto: `channel = 0`)

---

## Checklist de Implementação

- [ ] Passo 1: enum `ChannelPolicy` em `espnow_types.hpp`
- [ ] Passo 2: `set_channel_policy()` / `get_channel_policy()` em `IDiscoveryManager`
- [ ] Passo 3: implementação em `DiscoveryManager` (hpp + cpp)
- [ ] Passo 4: `set_channel_policy()` em `IEspNowManager` + `EspNowManager`
- [ ] Passo 5: remover `wifi_set_channel()` de `EspNowDriver::init()`
- [ ] Passo 6: uso na aplicação (`main.cpp` / water-tank)
- [ ] Testes: novo caso `ScanChannelSkipsWhenPolicyFixed`
- [ ] Testes: mock atualizado com novos métodos virtuais
- [ ] Build e host_tests passando
- [ ] Commit + push do `espnow_manager` e `smart-farm-water-tank`
