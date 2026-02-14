#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "esp_now.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

#include "espnow_interfaces.hpp"
#include "espnow_storage.hpp"
#include "espnow_types.hpp"
#include "protocol_messages.hpp"
#include "protocol_types.hpp"

// Configuration to initialize the EspNow component
struct EspNowConfig
{
    NodeId node_id;
    NodeType node_type;
    QueueHandle_t app_rx_queue;
    uint8_t wifi_channel;
    uint32_t ack_timeout_ms;
    uint32_t heartbeat_interval_ms;

    uint32_t stack_size_rx_dispatch;
    uint32_t stack_size_transport_worker;
    uint32_t stack_size_tx_manager;

    // Default constructor
    EspNowConfig()
        : node_id(ReservedIds::HUB)
        , node_type(ReservedTypes::UNKNOWN)
        , app_rx_queue(nullptr)
        , wifi_channel(DEFAULT_WIFI_CHANNEL)
        , ack_timeout_ms(DEFAULT_ACK_TIMEOUT_MS)
        , heartbeat_interval_ms(DEFAULT_HEARTBEAT_INTERVAL_MS)
        , stack_size_rx_dispatch(4096)
        , stack_size_transport_worker(5120)
        , stack_size_tx_manager(4096)
    {
    }
};

// Main class for ESP-NOW communication.
class EspNow
{
public:
    // Singleton
    static EspNow &instance();

    // Dependency injection constructor for testing
    EspNow(std::unique_ptr<IPeerManager> peer_manager,
           std::unique_ptr<ITxManager> tx_manager,
           IChannelScanner *scanner_ptr,
           std::unique_ptr<IMessageCodec> message_codec,
           std::unique_ptr<IHeartbeatManager> heartbeat_manager,
           std::unique_ptr<IPairingManager> pairing_manager,
           std::unique_ptr<IMessageRouter> message_router);

    EspNow(const EspNow &)            = delete;
    EspNow &operator=(const EspNow &) = delete;
    ~EspNow();

    // Public API
    esp_err_t init(const EspNowConfig &config);
    esp_err_t deinit();
    esp_err_t send_data(NodeId dest_node_id,
                        PayloadType payload_type,
                        const void *payload,
                        size_t len,
                        bool require_ack = false);

    template <typename T1, typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(PayloadType)>>
    esp_err_t send_data(T1 dest_node_id,
                        T2 payload_type,
                        const void *payload,
                        size_t len,
                        bool require_ack = false)
    {
        return send_data(static_cast<NodeId>(dest_node_id),
                         static_cast<PayloadType>(payload_type),
                         payload,
                         len,
                         require_ack);
    }

    esp_err_t send_command(NodeId dest_node_id,
                           CommandType command_type,
                           const void *payload,
                           size_t len,
                           bool require_ack = false);

    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t send_command(T dest_node_id,
                           CommandType command_type,
                           const void *payload,
                           size_t len,
                           bool require_ack = false)
    {
        return send_command(static_cast<NodeId>(dest_node_id),
                            command_type,
                            payload,
                            len,
                            require_ack);
    }

    esp_err_t confirm_reception(AckStatus status);

    // Peer Management Functions
    esp_err_t add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type);

    template <typename T1, typename T2,
              typename = std::enable_if_t<std::is_enum_v<T1> && sizeof(T1) == sizeof(NodeId)>,
              typename = std::enable_if_t<std::is_enum_v<T2> && sizeof(T2) == sizeof(NodeType)>>
    esp_err_t add_peer(T1 node_id, const uint8_t *mac, uint8_t channel, T2 type)
    {
        return add_peer(static_cast<NodeId>(node_id), mac, channel, static_cast<NodeType>(type));
    }

    esp_err_t remove_peer(NodeId node_id);

    template <typename T, typename = std::enable_if_t<std::is_enum_v<T> && sizeof(T) == sizeof(NodeId)>>
    esp_err_t remove_peer(T node_id)
    {
        return remove_peer(static_cast<NodeId>(node_id));
    }
    std::vector<PeerInfo> get_peers();
    std::vector<NodeId> get_offline_peers() const;
    esp_err_t start_pairing(uint32_t timeout_ms = 30000);

    bool is_initialized() const { return is_initialized_; }

protected:
    // --- Notification Bits ---
    static constexpr uint32_t NOTIFY_STOP = 0x100;

    // --- Private Members ---
    EspNowConfig config_{};

    std::unique_ptr<IPeerManager> peer_manager_;
    std::unique_ptr<ITxManager> tx_manager_;
    IChannelScanner *scanner_ptr_ = nullptr; // For updating node info in init()
    std::unique_ptr<IMessageCodec> message_codec_;
    std::unique_ptr<IHeartbeatManager> heartbeat_manager_;
    std::unique_ptr<IPairingManager> pairing_manager_;
    std::unique_ptr<IMessageRouter> message_router_;

    SemaphoreHandle_t ack_mutex_ = nullptr;
    bool is_initialized_         = false;
    bool esp_now_initialized_    = false;
    std::optional<MessageHeader> last_header_requiring_ack_{};

    QueueHandle_t rx_dispatch_queue_           = nullptr;
    QueueHandle_t transport_worker_queue_      = nullptr;
    TaskHandle_t rx_dispatch_task_handle_      = nullptr;
    TaskHandle_t transport_worker_task_handle_ = nullptr;

protected:
    // --- Private Methods ---
    uint64_t get_time_ms() const;

    // Persistence helpers
    void update_wifi_channel(uint8_t channel);

    // Task functions
    static void rx_dispatch_task(void *arg);
    static void transport_worker_task(void *arg);

    // Static ESP-NOW callbacks (ISR context)
    static void esp_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void esp_now_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status);
};
