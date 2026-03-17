# Análise de Alocação Dinâmica de Memória - Componente espnow_manager

Este documento detalha todos os pontos de alocação dinâmica de memória identificados no componente `espnow_manager`, categorizados por momento de execução, conforme solicitado.

---

## 1. Alocações de Inicialização (Setup)

Estas alocações ocorrem apenas uma vez durante a criação do Singleton ou na chamada do método `init()`. Elas apresentam baixo risco de fragmentação, mas definem o footprint base de memória do componente.

| Arquivo | Linha | Tipo de Alocação | Contexto / Descrição |
| :--- | :--- | :--- | :--- |
| `src/espnow_manager.cpp` | 45-63 | `std::make_unique` | Criação de todas as instâncias dos sub-componentes (backends, HALs, Managers) no Singleton. |
| `src/bootstrapper.cpp` | 80 | `mutex_create` | Criação do mutex de ACK. |
| `src/bootstrapper.cpp` | 87 | `queue_create` | Fila de recepção (`rx_queue`) de pacotes brutos. |
| `src/bootstrapper.cpp` | 92 | `queue_create` | Fila de trabalho (`worker_queue`) para processamento de protocolos. |
| `src/bootstrapper.cpp` | 99 | `task_create` | Task de dispatch de recepção (`rx_dispatch_task`). |
| `src/bootstrapper.cpp` | 111 | `task_create` | Task de worker de transporte (`worker_task`). |
| `src/tx_manager.cpp` | 43 | `queue_create` | Fila de transmissão (`tx_queue`). |
| `src/tx_manager.cpp` | 48 | `semaphore_create_binary` | Semáforo de sincronização para finalização da task. |
| `src/tx_manager.cpp` | 55 | `timer_create` | Timer de software para timeout de ACK. |
| `src/tx_manager.cpp` | 62 | `task_create` | Task principal do gerenciador de TX (`tx_manager_task`). |
| `src/peer_manager.cpp` | 22 | `mutex_create` | Mutex para proteção da lista de peers. |
| `src/peer_manager.cpp` | 23 | `vector::reserve` | Alocação inicial de capacidade para o vetor de peers (heap). |
| `src/heartbeat_manager.cpp` | 47 | `timer_create` | Timer de software para disparar heartbeats periódicos. |

---

## 2. Alocações em Tempo de Execução (Steady-state)

Estas alocações ocorrem durante a operação normal do sistema. São os pontos de maior atenção para fragmentação de heap, especialmente em uso intensivo.

| Arquivo | Linha | Tipo de Alocação | Contexto / Descrição |
| :--- | :--- | :--- | :--- |
| `src/peer_manager.cpp` | 181 | `std::vector` (copy) | O método `get_all()` retorna uma cópia completa do vetor `peers_`. |
| `src/peer_manager.cpp` | 192 | `std::vector` (temp) | Criação de vetor temporário para listar nós offline em `get_offline()`. |
| `src/peer_manager.cpp` | 222 | `std::vector` (temp) | Vetor temporário para carregar dados do storage em `load_from_storage()`. |
| `src/peer_manager.cpp` | 249 | `std::vector` (temp) | Vetor temporário para converter e salvar peers em `save_to_storage()`. |
| `src/espnow_manager.cpp` | 424 | `std::vector` (return) | O método público `get_peers()` expõe a cópia de `peer_manager_->get_all()`. |
| `src/espnow_manager.cpp` | 429 | `std::vector` (return) | O método público `get_offline_peers()` retorna o vetor gerado pelo `peer_manager`. |
| `src/peer_manager.cpp` | 118 | `vector::insert` | Inserção de novo peer. Embora o `reserve` minimize, se houver muitos ciclos de `add`/`remove`, pode haver reallocs internos dependendo da implementação da STL. |

---

## 3. Chamadas de APIs ESP-IDF (Alocação Interna)

Estas funções do SDK do ESP32 realizam alocações dinâmicas internamente (no heap do sistema ou em pools específicos do driver).

- `esp_now_init()`: Aloca estruturas de controle do driver ESP-NOW.
- `esp_now_add_peer()`: Aloca memória para armazenar informações do peer na tabela do driver (limite de 20 no ESP32).
- `esp_now_del_peer()`: Libera a memória associada ao peer no driver.
- `nvs_flash_init()`: Aloca buffers de cache e estruturas de controle para o NVS.
- `nvs_open()`: Aloca handles e buffers para operações em namespaces NVS.
- `nvs_set_blob()`: Pode alocar buffers temporários para escrita na flash.

---

## 4. Avaliação e Riscos

### Fragmentação de Heap
O risco de fragmentação no componente é **Moderado**.
- A maioria das alocações FreeRTOS (tasks, filas, mutexes) é estática após a inicialização, o que é excelente para estabilidade.
- O ponto crítico identificado é o uso de `std::vector` no `PeerManager` e `EspNowManager`. Toda vez que `get_all()` ou `get_peers()` é chamado, um novo bloco de memória é alocado no heap para a cópia, e liberado logo em seguida. Em sistemas com pouco heap disponível e muitas chamadas a esses métodos, isso pode levar à fragmentação.

### Recomendações (Opcional)
1. **Evitar Cópias de Vetores:** Alterar métodos como `get_all()` para retornar referências constantes ou usar callbacks (iteradores) para processar peers sem alocar novos vetores.
2. **Substituir `std::vector`:** Utilizar `etl::vector` (Embedded Template Library) ou uma implementação baseada em array estático com tamanho máximo `MAX_PEERS`, eliminando totalmente as alocações em tempo de execução no `PeerManager`.
3. **Static Allocation FreeRTOS:** Se a versão do ESP-IDF permitir, as filas e tasks poderiam usar as variantes `*_create_static` para garantir que a memória venha de buffers pré-alocados no segmento de dados, zerando o risco de falha por falta de heap após o boot.
