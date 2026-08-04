# Design Note: `wifi_channel` no EspNowManager

> Questão levantada ao analisar a convivência de WiFi STA + ESP-NOW simultâneos e a introdução do `ChannelPolicy`.

---

## Contexto

Com a adição do `ChannelPolicy::FIXED` (nó conectado a AP: não varre canais, herda o canal do AP), surgiu a questão de se ainda faz sentido manter três mecanismos de gerenciamento de canal dentro do `EspNowManager`:

1. `config_.wifi_channel`
2. `scanner_->set_channel()`
3. `storage_->store_channel()`

---

## Análise de Cada Um

### 1. `config_.wifi_channel`

**O que é hoje**: Membro mutável de `EspNowConfig` que serve como "canal corrente conhecido" pelo `EspNowManager`. É lido e escrito em vários pontos:

| Ponto | O que faz |
|---|---|
| `init()` L166 | Sobreposto pelo canal carregado do NVS (se existir) |
| `espnow_driver.cpp` L45 | Passado ao `wifi_set_channel()` — **o ponto problemático** |
| `init()` L254 | Usado para setar o `scanner_` ponto-de-início |
| `NOTIFY_CHANNEL_FOUND` L693 | Atualizado ao descobrir o hub via varredura |
| `NOTIFY_CHANNEL_CHANGED` L731 | Atualizado quando `ChannelMonitor` detecta mudança |

**Tensão**: O campo existe porque o `EspNowDriver` precisava de um canal para chamar `wifi_set_channel()`. Com `ChannelPolicy::FIXED`, essa chamada some. Sem ela, `config_.wifi_channel` torna-se um "espelho" do canal real — uma informação que a aplicação já tem (ela sabe em qual AP está conectada), duplicada dentro do `EspNowManager` sem utilidade clara.

**Conclusão futura**: Com `ChannelPolicy::FIXED` padrão para nós conectados, `wifi_channel` em `EspNowConfig` pode ser removido do `EspNowDriver`. O `config_.wifi_channel` poderia se tornar somente leitura (dado pela varredura ou pelo `ChannelMonitor`), não mais uma entrada de configuração da aplicação.

---

### 2. `scanner_->set_channel()` (ponto de início da varredura)

**O que é hoje**: Diz ao `DiscoveryManager` por qual canal começar a varredura. A lógica em `scan_channel()` inicia pelo `current_channel_` e percorre os 13 canais em offset circular:

```cpp
uint8_t channel = ((current_channel_.load() - 1 + offset) % 13) + 1;
```

**Valor prático**: Se o canal do AP raramente muda, começar pelo último canal conhecido economiza algumas tentativas (o hub é encontrado na primeira iteração). Varrer 13 canais × `SCAN_CHANNEL_ATTEMPTS` tentativas já é rápido em termos de tempo absoluto (~1-3 segundos), então o ganho é marginal.

> "é muito rápido varrer todos os canais" — exato. Otimização prematura.

**Conclusão futura**: Quando `ChannelPolicy::SCAN` estiver em vigor, o `DiscoveryManager` poderia simplesmente começar sempre do canal 1, eliminando a necessidade de `set_channel()` externo. O `ChannelMonitor` continua relevante para detectar mudanças, mas não precisa propagar via `scanner_->set_channel()` — só precisaria informar a aplicação se ela quiser reagir.

---

### 3. `storage_->store_channel()` / `load_channel()`

**O que é hoje**: Persiste o canal no NVS em dois momentos:

| Ponto | Quando |
|---|---|
| `init()` L255 | Ao inicializar (salva o canal de config/NVS de volta) |
| `NOTIFY_CHANNEL_CHANGED` L733 | Quando `ChannelMonitor` detecta mudança de canal |
| `handle_state_transition()` L775 | Ao entrar em `OPERATIONAL` vindo de `RECOVERY_SCAN` ou `PAIRING_SCAN` |

**Intenção original**: Que, no próximo boot, o `DiscoveryManager` começasse a varredura pelo canal onde o hub foi encontrado antes — evitando re-escanear do zero.

**Faz sentido?** Depende do cenário:

| Cenário | Utilidade |
|---|---|
| Nó standalone (sem AP), hub também standalone | ✅ Útil: hub provavelmente está no mesmo canal de antes |
| Nó conectado ao AP (modo FIXED) | ❌ Irrelevante: o canal é determinado pelo AP, não pelo NVS |
| Roteador com auto-channel (troca de canal no reboot) | ❌ Irrelevante: canal armazenado ficará obsoleto mesmo para modo SCAN |
| Roteador com canal fixo + hub standalone | ✅ Útil apenas se o hub for autônomo e não conectado |

**Conclusão**: A maioria dos roteadores modernos muda de canal ao reiniciar, tornando o canal armazenado stale na maioria dos casos reais. Em produção com AP fixo + `ChannelPolicy::FIXED`, o canal é irrelevante para o NVS.

> O `store_channel` tem utilidade real apenas no cenário clássico "dois ESP32 standalone + ESP-NOW puro", onde o canal do hub raramente muda entre reboots. Em qualquer configuração com AP real, o valor é sempre stale após um reset do roteador.

---

## Resumo da Recomendação Futura

| Elemento | Status atual | Recomendação futura |
|---|---|---|
| `config_.wifi_channel` em `EspNowConfig` | Entrada obrigatória + estado mutável | Remover como entrada; manter apenas como estado interno read-only derivado da varredura ou do `ChannelMonitor` |
| `espnow_driver` chama `wifi_set_channel()` | Sempre, no init | Remover; a política de canal não é responsabilidade do EspNow |
| `scanner_->set_channel()` | Otimização de ponto de início | Pode ser eliminado; varredura completa é rápida o suficiente |
| `storage_->store_channel()` | Persiste canal para próxima boot | Útil só em ESP-NOW puro standalone; irrelevante com AP. Candidato a remoção condicional ou eliminação total |
| `ChannelMonitor` | Detecta mudança de canal via `wifi_get_channel()` | Mantém função útil mesmo com `ChannelPolicy::FIXED`: notifica a aplicação se o AP mudou de canal inesperadamente |

---

## O Que Não Mudar Agora

- A refatoração completa (remover `wifi_channel` do `EspNowConfig`, eliminar `store_channel`, simplificar varredura) é uma tarefa separada e significativa.
- O `ChannelPolicy` proposto (Opção Push via `set_channel_policy()`) resolve o problema imediato — impedir a varredura quando conectado — sem precisar mexer nessa estrutura agora.
- Esse documento serve como referência para quando for feita uma limpeza arquitetural mais ampla do `espnow_manager`.
