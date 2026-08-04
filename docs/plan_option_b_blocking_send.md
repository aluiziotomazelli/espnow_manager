# Plano Revisado: Opção B — `send_data` bloqueante implícito (sem mudança de API pública)

## Restrições do design

- **Zero mudança na API pública** (`IEspNowManager`, `IEspNowManager::send_data`, `ITxManager::queue_packet`).
- Timeout derivado de `ack_timeout_ms` (já em `TxManager::ack_timeout_ms_`) × `MAX_FAILURES` — sem parâmetro novo.
- Quando `require_ack=true`, `send_data` **sempre** bloqueia (corrige o doc que já prometia isso).
- Quando `require_ack=false`, comportamento idêntico ao atual.

---

## Retornos distintos: `ESP_ERR_TIMEOUT` vs `ESP_FAIL`

A distinção semântica foi revelada ao analisar os dois caminhos de falha da FSM:

| Caminho                                | Causa raiz                                                                                                                  | Retorno           |
| -------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- | ----------------- |
| `on_max_retries()`                     | ACK lógico nunca chegou após todos os retries — nó alcançável fisicamente mas não respondendo (ex: app travada no receptor) | `ESP_ERR_TIMEOUT` |
| `on_delivery_failure()` retorna `true` | Falha física repetida (ESP-NOW callback `SEND_FAIL`) — hub fora do canal ou fora de alcance                                 | `ESP_FAIL`        |
| `on_ack_received()`                    | ACK recebido com sucesso                                                                                                    | `ESP_OK`          |

Nota: `on_max_retries()` não envia `NOTIFY_MAX_FAILURES` para a rx_task — apenas reseta a FSM
para IDLE. Portanto, sem a nossa sinalização via EventGroup, o chamador nunca saberia desse evento.

---

## Por que não reusar os bits de `task_notify` diretamente?

```
tx_task ──task_notify(tx_task_handle_, NOTIFY_LOGICAL_ACK)──► tx_task
```

Os bits existentes (`NOTIFY_LOGICAL_ACK`, `NOTIFY_ACK_TIMEOUT`, `NOTIFY_MAX_FAILURES`) são
enviados **para a própria `tx_task`** — não para a task chamadora de `queue_packet`.
A task chamadora roda em outro contexto (ex: `app_task`), portanto:

- `task_notify_wait` na task chamadora **nunca receberia** esses bits.
- Mesmo criando bits novos e enviando para a task chamadora com `get_task_handle()`, a task
  chamadora pode ter seu próprio loop de notificações que seria interferido.

**Conclusão**: precisamos de um primitivo isolado. O mais simples e sem efeitos colaterais é
um **EventGroup efêmero** — criado para o ciclo de vida de um único envio bloqueante.

---

## Mecanismo: EventGroup efêmero injetado via `DecodedTxPacket`

```
chamador (app_task)
  └─ queue_packet(requires_ack=true)
        ├─ cria EventGroupHandle_t eg
        ├─ injeta eg em DecodedTxPacket.ack_event_group
        ├─ enfileira pacote
        └─ event_group_wait_bits(eg, ACK|TIMEOUT|FAIL, timeout)  ← bloqueia

tx_task
  ├─ dequeue → captura pending_ack_event_group_ = packet.ack_event_group
  └─ handle_notifications(NOTIFY_LOGICAL_ACK)
        └─ event_group_set_bits(pending_ack_event_group_, NOTIFY_LOGICAL_ACK)
              ↑ desbloqueia o chamador

chamador
  ├─ lê os bits do wait → mapeia resultado
  └─ event_group_delete(eg)  ← limpeza
```

### Por que o EventGroup é seguro aqui?

- Criado **antes** de enfileirar → existe quando `tx_task` precisar escrever nele.
- Deletado **pelo chamador após** o `wait_bits` retornar → `tx_task` já sinalizou, zeramos
  `pending_ack_event_group_` imediatamente após o `set_bits`.
- Sem possibilidade de double-free: `tx_task` zera o ponteiro após sinalizar; `queue_packet`
  deleta após o wait.

---

## Cálculo do timeout interno

```cpp
// ack_timeout_ms_ já armazenado no TxManager
// MAX_FAILURES = 3 (retries)
// + 1 para a tentativa inicial
// + margem de encoding/send (~50 ms cada)
const TickType_t wait_ticks =
    pdMS_TO_TICKS(ack_timeout_ms_ * (MAX_FAILURES + 1) + 200);
```

Com defaults (`300 ms × 4 + 200 ms = 1400 ms`), comporta todas as tentativas com folga.

---

## Arquivos Afetados

### 1. `include/interfaces/i_en_hal_freertos.hpp` — 4 novos métodos

```cpp
/** @copydoc xEventGroupCreate() */
virtual EventGroupHandle_t event_group_create() = 0;

/** @copydoc xEventGroupSetBits() */
virtual EventBits_t event_group_set_bits(EventGroupHandle_t eg, EventBits_t bits) = 0;

/** @copydoc xEventGroupWaitBits() */
virtual EventBits_t event_group_wait_bits(
    EventGroupHandle_t eg,
    EventBits_t        bits_to_wait,
    BaseType_t         clear_on_exit,
    BaseType_t         wait_all_bits,
    TickType_t         ticks_to_wait) = 0;

/** @copydoc vEventGroupDelete() */
virtual void event_group_delete(EventGroupHandle_t eg) = 0;
```

> [!NOTE]
> Requer `#include "freertos/event_groups.h"` no header.
> O include já é compatível com a regra "Single Header Rule" pois `event_groups.h`
> é parte do FreeRTOS, o mesmo subsistema já coberto por este HAL.

---

### 2. `include/en_hal_freertos.hpp` + `src/en_hal_freertos.cpp`

Implementações triviais (1:1 wrappers):

```cpp
// .hpp
EventGroupHandle_t event_group_create() override;
EventBits_t event_group_set_bits(EventGroupHandle_t eg, EventBits_t bits) override;
EventBits_t event_group_wait_bits(EventGroupHandle_t eg, EventBits_t bits,
                                   BaseType_t clear_on_exit, BaseType_t wait_all,
                                   TickType_t ticks) override;
void event_group_delete(EventGroupHandle_t eg) override;

// .cpp
EventGroupHandle_t FreeRTOSHAL::event_group_create() { return xEventGroupCreate(); }
EventBits_t FreeRTOSHAL::event_group_set_bits(EventGroupHandle_t eg, EventBits_t bits)
    { return xEventGroupSetBits(eg, bits); }
EventBits_t FreeRTOSHAL::event_group_wait_bits(EventGroupHandle_t eg, EventBits_t bits,
    BaseType_t c, BaseType_t w, TickType_t t)
    { return xEventGroupWaitBits(eg, bits, c, w, t); }
void FreeRTOSHAL::event_group_delete(EventGroupHandle_t eg) { vEventGroupDelete(eg); }
```

---

### 3. `host_test_common/mock_hal_freertos.hpp`

```cpp
MOCK_METHOD(EventGroupHandle_t, event_group_create, (), (override));
MOCK_METHOD(EventBits_t, event_group_set_bits, (EventGroupHandle_t, EventBits_t), (override));
MOCK_METHOD(EventBits_t, event_group_wait_bits,
    (EventGroupHandle_t, EventBits_t, BaseType_t, BaseType_t, TickType_t), (override));
MOCK_METHOD(void, event_group_delete, (EventGroupHandle_t), (override));
```

---

### 4. `include/espnow_types.hpp` — Campo opcional em `DecodedTxPacket`

```diff
+#include "freertos/event_groups.h"

 struct DecodedTxPacket
 {
     uint8_t dest_mac[6];
     MessageHeader header;
     uint8_t payload[MAX_PAYLOAD_SIZE];
     size_t  payload_len;
+    /// Se != nullptr, tx_task sinaliza aqui o resultado do envio (ACK/timeout/fail).
+    /// Gerenciado inteiramente por queue_packet — não tocar fora dele.
+    EventGroupHandle_t ack_event_group = nullptr;
 };
```

---

### 5. `include/tx_manager.hpp` — Campo privado

```diff
     QueueHandle_t delivery_queue_ = nullptr;
+    // Handle do EventGroup do chamador bloqueante atual (nullptr se não há).
+    // Capturado do DecodedTxPacket ao desencafileirar; zerado após sinalizar.
+    EventGroupHandle_t pending_ack_event_group_ = nullptr;
```

---

### 6. `src/tx_manager.cpp` — `queue_packet`: lógica bloqueante

```cpp
esp_err_t TxManager::queue_packet(const DecodedTxPacket& packet)
{
    if (tx_queue_ == nullptr)
        return ESP_ERR_INVALID_STATE;

    const bool blocking = packet.header.requires_ack;

    EventGroupHandle_t eg = nullptr;
    DecodedTxPacket pkt_copy = packet;

    if (blocking) {
        eg = freertos_hal_.event_group_create();
        if (eg == nullptr)
            return ESP_ERR_NO_MEM;
        pkt_copy.ack_event_group = eg;
    }

    if (freertos_hal_.queue_send(tx_queue_, &pkt_copy, 100) != pdTRUE) {
        if (eg != nullptr)
            freertos_hal_.event_group_delete(eg);
        return ESP_FAIL;
    }

    if (tx_task_handle_ != nullptr)
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_DATA, eSetBits);

    if (!blocking)
        return ESP_OK;

    // Aguarda resolução: ACK recebido, retries esgotados ou timeout interno.
    const EventBits_t RESULT_BITS = NOTIFY_LOGICAL_ACK | NOTIFY_ACK_TIMEOUT | NOTIFY_MAX_FAILURES;
    const TickType_t wait_ticks = pdMS_TO_TICKS(ack_timeout_ms_ * (MAX_FAILURES + 1) + 200);

    EventBits_t bits = freertos_hal_.event_group_wait_bits(
        eg,
        RESULT_BITS,
        pdFALSE,  // não limpa bits — o grupo é deletado a seguir
        pdFALSE,  // qualquer bit (não exige todos)
        wait_ticks);

    freertos_hal_.event_group_delete(eg);

    if (bits & NOTIFY_LOGICAL_ACK)   return ESP_OK;
    if (bits & NOTIFY_MAX_FAILURES)  return ESP_FAIL;
    // bits == 0: wait expirou sem resposta (não deveria acontecer com timeout bem calculado)
    return ESP_ERR_TIMEOUT;
}
```

---

### 7. `src/tx_manager.cpp` — `tx_task()` IDLE: capturar EventGroup

```diff
 if (freertos_hal_.queue_receive(tx_queue_, &structured_packet, 0) == pdTRUE) {
+    // Captura o event group do pacote (nullptr se chamada não-bloqueante)
+    pending_ack_event_group_ = structured_packet.ack_event_group;
```

---

### 8. `src/tx_manager.cpp` — Dois pontos de sinalização do EventGroup

#### 8a. `handle_notifications` — ACK recebido (caminho feliz)

```diff
 if ((notifications & NOTIFY_LOGICAL_ACK) == NOTIFY_LOGICAL_ACK) {
     fsm_.on_ack_received();
     freertos_hal_.timer_stop(ack_timeout_timer_, pdMS_TO_TICKS(10));
+    if (pending_ack_event_group_ != nullptr) {
+        freertos_hal_.event_group_set_bits(pending_ack_event_group_, NOTIFY_LOGICAL_ACK);
+        pending_ack_event_group_ = nullptr;
+    }
 }
```

#### 8b. `handle_notifications` — Falha física: delivery failure esgotado → `ESP_FAIL`

`on_delivery_failure()` retorna `true` quando `send_fail_count_ >= MAX_FAILURES` (falha no
callback ESP-NOW). Significa que o frame não chegou ao peer em nível físico — hub fora do canal.

```diff
 bool max_failures = fsm_.on_delivery_failure();
 if (max_failures) {
     ESP_LOGW(TAG, "Max failures reached, notifying RX task");
     freertos_hal_.task_notify(rx_task_handle_, NOTIFY_MAX_FAILURES, eSetBits);
+    if (pending_ack_event_group_ != nullptr) {
+        freertos_hal_.event_group_set_bits(pending_ack_event_group_, NOTIFY_MAX_FAILURES);
+        pending_ack_event_group_ = nullptr;
+    }
 }
```

#### 8c. Bloco `TxState::RETRYING` em `tx_task()` — Retries de ACK esgotados → `ESP_ERR_TIMEOUT`

`on_max_retries()` é chamado quando `retries_left == 0` no bloco `RETRYING`. Nesse ponto o
frame chegou fisicamente (delivery success), mas o peer nunca enviou o ACK lógico de volta.
`on_max_retries()` **não emite nenhuma notificação** — apenas reseta a FSM para IDLE.
Portanto é aqui que sinalizamos o EventGroup com o bit de timeout:

```diff
 } else {
     // Retries exhausted, notify FSM to drop the packet and potentially sever the link state.
     auto pending_opt = fsm_.get_pending_ack();
     if (pending_opt) {
         stats_mgr_.on_packet_lost(pending_opt->node_id);
     }
     fsm_.on_max_retries();
+    // Sinaliza o chamador bloqueante: ACK lógico nunca chegou após todos os retries.
+    // ESP_ERR_TIMEOUT porque o peer era alcançável fisicamente, mas não respondeu.
+    if (pending_ack_event_group_ != nullptr) {
+        freertos_hal_.event_group_set_bits(pending_ack_event_group_, NOTIFY_ACK_TIMEOUT);
+        pending_ack_event_group_ = nullptr;
+    }
 }
```

> [!IMPORTANT]
> O `NOTIFY_ACK_TIMEOUT` que chega via `handle_notifications` (timer de uma tentativa) **não
> sinaliza** o EventGroup — ele apenas transita a FSM para `RETRYING`. A sinalização de
> timeout definitivo acontece no bloco `RETRYING` quando `retries_left == 0`, após
> `on_max_retries()`. Dois contextos, mesmo bit, semânticas distintas dentro do EventGroup.

#### Mapeamento final nos bits do EventGroup → retorno do `queue_packet`

```
NOTIFY_LOGICAL_ACK   → ESP_OK           (ACK recebido)
NOTIFY_ACK_TIMEOUT   → ESP_ERR_TIMEOUT  (retries esgotados: peer alcançável, não respondeu)
NOTIFY_MAX_FAILURES  → ESP_FAIL         (falha física: peer fora do canal ou fora de alcance)
bits == 0            → ESP_ERR_TIMEOUT  (watchdog interno: nunca deve acontecer)
```

---

### 9. `include/interfaces/i_espnow_manager.hpp` — Corrigir doc (única mudança aqui)

```diff
- * @note Non-blocking unless require_ack=true
+ * @note If `require_ack=false`, this method is non-blocking and returns ESP_OK as soon as
+ *       the packet is queued for transmission.
+ * @note If `require_ack=true`, this method blocks the calling task until a logical ACK is
+ *       received from the destination, or until all retransmission attempts are exhausted.
+ * @return ESP_FAIL: all retransmission attempts failed without receiving a logical ACK.
```

---

## Resumo dos Arquivos Afetados

| Arquivo                                    | Mudança                                                                               | Tamanho  |
| ------------------------------------------ | ------------------------------------------------------------------------------------- | -------- |
| `include/interfaces/i_en_hal_freertos.hpp` | + 4 métodos EventGroup                                                                | Pequena  |
| `include/en_hal_freertos.hpp`              | + 4 declarações                                                                       | Pequena  |
| `src/en_hal_freertos.cpp`                  | + 4 wrappers 1:1                                                                      | Pequena  |
| `host_test_common/mock_hal_freertos.hpp`   | + 4 MOCK_METHODs                                                                      | Pequena  |
| `include/espnow_types.hpp`                 | + `ack_event_group` em `DecodedTxPacket`                                              | 2 linhas |
| `include/tx_manager.hpp`                   | + `pending_ack_event_group_` privado                                                  | 2 linhas |
| `src/tx_manager.cpp`                       | Lógica em `queue_packet`, captura em `tx_task`, sinalização em `handle_notifications` | Média    |
| `include/interfaces/i_espnow_manager.hpp`  | Corrigir `@note` do `send_data`/`send_command`                                        | Doc only |

**Não muda:** `ITxManager::queue_packet`, `IEspNowManager::send_data`, `IEspNowManager::send_command`,
`EspNowManager::send_data`, `EspNowManager::send_command`, `EspNowManager::send_packet`.

---

## Pontos de Atenção

> [!WARNING]
> `NOTIFY_ACK_TIMEOUT` sinaliza timeout de *uma tentativa* — não deve desbloquear o chamador.
> Somente `NOTIFY_LOGICAL_ACK` e `NOTIFY_MAX_FAILURES` representam resultados definitivos.

> [!WARNING]
> Se o `TxManager` for `deinit()`ado enquanto há um chamador bloqueado em `wait_bits`,
> o `deinit()` deve sinalizar o EventGroup com `NOTIFY_MAX_FAILURES` antes de encerrar a task.
> Adicionar ao bloco `NOTIFY_TASK_TO_STOP` em `handle_notifications`:
> ```cpp
> if (pending_ack_event_group_ != nullptr) {
>     freertos_hal_.event_group_set_bits(pending_ack_event_group_, NOTIFY_MAX_FAILURES);
>     pending_ack_event_group_ = nullptr;
> }
> ```
