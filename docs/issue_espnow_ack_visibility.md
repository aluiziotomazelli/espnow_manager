# Issue: ACK lógico não é exposto para a camada de aplicação

**Componente:** `espnow_manager`  
**Arquivo Principal:** [`i_espnow_manager.hpp`]  
**Prioridade:** Média-Alta  
**Tipo:** Enhancement / Design Gap

---

## Contexto

O `espnow_manager` suporta um protocolo de **ACK lógico** (camada de aplicação) de duas vias:

- O **remetente** chama `send_data(..., require_ack=true)` e o `TxManager` aguarda um pacote `MessageType::ACK` de volta.
- O **destinatário**, ao processar a mensagem, chama `confirm_reception(sender_id, seq, AckStatus::OK)` para enviar o ACK.

O propósito do ACK lógico é exatamente garantir que o destinatário recebeu, processou e reconheceu a mensagem — não apenas que o frame foi entregue ao nível físico/Wi-Fi.

---

## Problema

### Hoje: o ACK é totalmente invisível para o remetente

O ciclo de vida atual do ACK no remetente é:

```
send_data(require_ack=true)
  └─> send_packet()
        └─> tx_manager_.queue_packet()   ← retorna ESP_OK imediatamente
              └─> [background: tx_task() envia o frame]
                    └─> WAITING_FOR_ACK
                          ├── ACK recebido → TxState::IDLE   ← app não sabe
                          └── Timeout × 3  → NOTIFY_MAX_FAILURES → RECOVERY_SCAN
```

O retorno de `send_data()` é `ESP_OK` assim que o pacote é enfileirado, **antes de qualquer tentativa de transmissão**. O resultado definitivo do envio com ACK — sucesso ou falha — é consumido inteiramente **dentro do `TxManager`** e nunca sobe para a interface pública.

A única forma atual de inferir o resultado é:
- Verificar `get_node_state() == RECOVERY_SCAN` **depois de um delay externo** (ex: a janela de listen de 200ms);
- Confiar que o nó ainda está em `OPERATIONAL` como prova implícita de que o ACK chegou.

### Evidências no código

**[`rx_task` em `espnow_manager.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/src/espnow_manager.cpp#L572-L585)** — pacotes `ACK` (`MessageType::ACK`) são roteados internamente e **nunca chegam à `app_rx_queue`**:

```cpp
// Application-level packets — deliver directly to app queue
if (decoded.header.msg_type == MessageType::DATA ||
    decoded.header.msg_type == MessageType::COMMAND) {
    // ...entrega para app_rx_queue...
}
else {
    // Protocol-internal packets — handle immediately via router
    self->message_router_->handle_packet(decoded);   // ACK entra aqui
}
```

**[`i_espnow_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/include/interfaces/i_espnow_manager.hpp#L102-L103)** — a própria doc do `send_data` admite isso:

```
* @note Non-blocking unless require_ack=true
```

> Nota: a doc diz "Non-blocking unless require_ack=true", mas na prática o método **sempre retorna imediatamente** mesmo com `require_ack=true`. A frase da doc está incorreta.

---

## Impacto

| Situação                                     | Comportamento Atual                              | Comportamento Desejado                                   |
| -------------------------------------------- | ------------------------------------------------ | -------------------------------------------------------- |
| ACK recebido com sucesso                     | App não é notificado                             | App deveria saber que a mensagem foi confirmada          |
| ACK não recebido após 3 tentativas           | Nó entra em `RECOVERY_SCAN` assincronamente      | App deveria saber que o envio falhou antes de dormir     |
| App quer aguardar confirmação antes de sleep | Precisa usar delays externos + polling do estado | Deveria poder aguardar o resultado do ACK explicitamente |

No caso do `WaterTankApp`, após chamar `send_report()` (com `require_ack=true`), o nó entra em deep sleep sem ter certeza se o Hub recebeu e confirmou o relatório. A "espera" pelos 200ms do `listen_for_messages()` é uma heurística, não uma garantia.

---

## Solução Proposta

### Opção A: Callback de resultado (Recomendada)

Adicionar um callback/handler opcional na `IEspNowManager` que é chamado quando um ACK é resolvido (sucesso ou falha):

```cpp
// Em espnow_types.hpp
struct AckResult {
    NodeId    dest_node_id;
    uint16_t  sequence_number;
    bool      success;       // true = ACK recebido, false = max retries expirado
    AckStatus ack_status;    // apenas válido se success == true
    uint32_t  rtt_ms;        // Round-trip time (0 se success == false)
};

using AckResultCallback = std::function<void(const AckResult&)>;

// Em IEspNowManager
virtual void set_ack_result_callback(AckResultCallback cb) = 0;
```

**Prós:**
- Não bloqueia a task chamadora — compatível com o design assíncrono do `TxManager`.
- App pode decidir o que fazer ao receber o callback (logar, marcar estado, etc.).
- Implementação localizada: apenas `TxManager` e `EspNowManager` precisam mudar.

**Contras:**
- Callback roda no contexto da `tx_task` interna — o app deve ser cuidadoso com thread safety.

### Opção B: `send_data` bloqueante opcional

Adicionar um parâmetro `timeout_ms` a `send_data()`. Se passado e `require_ack=true`, a função bloqueia até receber o ACK ou o timeout expirar:

```cpp
// Retorna ESP_OK se ACK recebido, ESP_ERR_TIMEOUT se expirar, ESP_FAIL se max retries
virtual esp_err_t send_data(
    NodeId dest_node_id,
    PayloadType payload_type,
    const void* payload,
    size_t len,
    bool require_ack = false,
    uint32_t ack_wait_timeout_ms = 0   // 0 = comportamento atual (não bloqueia)
) = 0;
```

**Prós:**
- API simples e idiomática — o retorno do próprio `send_data` informa o resultado.
- Sem threading extra para o chamador gerenciar.

**Contras:**
- Bloqueia a task chamadora (ex: app task) — pode exigir ajustes no watchdog timer.
- O `TxManager` precisaria de um mecanismo de sinalização (semáforo ou event group) para desbloquear o chamador.
- Mais invasivo na arquitetura atual.

### Opção C: `get_last_tx_result()` (Polling)

Expor o resultado do último envio ACK via getter síncrono:

```cpp
struct TxResult {
    bool     resolved;   // true se já foi resolvido (ACK ou falha)
    bool     success;
    uint16_t sequence_number;
};

virtual TxResult get_last_tx_result() const = 0;
```

**Prós:**
- Sem callbacks, sem bloqueio — compatível com o padrão de polling do `wait_for_comm_ready`.
- Implementação simples: `TxManager` guarda o último resultado atômico.

**Contras:**
- Sujeito a race condition se um segundo pacote for enviado antes do app verificar o resultado do primeiro.
- Exige que o app implemente seu próprio loop de polling com timeout.

---

## Recomendação

A **Opção A (callback)** é a mais limpa arquiteturalmente: respeita o design async do `TxManager`, não bloqueia a task do app, e permite que o resultado do ACK seja comunicado de forma determinística.

Para o caso de uso do `WaterTankApp`, o callback seria registrado no `init()` e atualizaria um flag atômico (`last_send_acked_`), que seria verificado após o `listen_for_messages()` — eliminando a dependência de tempo.

---

## Arquivos Afetados

| Arquivo                                                                                                                                                         | Mudança Necessária                                        |
| --------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| [`i_espnow_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/include/interfaces/i_espnow_manager.hpp) | Adicionar `set_ack_result_callback()`                     |
| [`espnow_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/include/espnow_manager.hpp)                | Implementar o método e guardar o callback                 |
| [`espnow_manager.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/src/espnow_manager.cpp)                    | Registrar callback no `TxManager` e disparar no ACK       |
| [`i_tx_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/include/interfaces/i_tx_manager.hpp)         | Adicionar `set_ack_result_callback()`                     |
| [`tx_manager.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/src/tx_manager.cpp)                            | Chamar callback quando ACK chegar ou max retries atingido |
| [`espnow_types.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/include/espnow_types.hpp)                    | Adicionar struct `AckResult`                              |
| [`water_tank_app.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/main/include/water_tank_app.hpp)                                     | Registrar callback no init, guardar `last_send_acked_`    |
| [`water_tank_app.cpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/main/src/water_tank_app.cpp)                                         | Usar `last_send_acked_` em vez de delay implícito         |

---

> [!NOTE]
> A doc do `send_data()` em [`i_espnow_manager.hpp`](file:///home/german/dev/workspaces/smart-farm/smart-farm-water-tank/components/espnow_manager/include/interfaces/i_espnow_manager.hpp#L102) afirma `@note Non-blocking unless require_ack=true`, o que é **incorreto** — o método sempre retorna imediatamente independentemente de `require_ack`. Isso deve ser corrigido como parte desta issue.
