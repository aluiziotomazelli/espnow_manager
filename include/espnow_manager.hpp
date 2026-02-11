#pragma once

#include "esp_now.h"
#include "espnow_interfaces.hpp"
#include "espnow_storage.hpp"
#include "espnow_types.hpp"
#include "protocol_messages.hpp"
#include "protocol_types.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"

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
        : node_id(NodeId::HUB)
        , node_type(NodeType::UNKNOWN)
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
           std::unique_ptr<ITxStateMachine> tx_state_machine,
           std::unique_ptr<IChannelScanner> channel_scanner,
           std::unique_ptr<IMessageCodec> message_codec);

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
    esp_err_t send_command(NodeId dest_node_id,
                           CommandType command_type,
                           const void *payload,
                           size_t len,
                           bool require_ack = false);
    esp_err_t confirm_reception(AckStatus status);

    // Peer Management Functions
    esp_err_t add_peer(NodeId node_id,
                       const uint8_t *mac,
                       uint8_t channel,
                       NodeType type);
    esp_err_t remove_peer(NodeId node_id);
    std::vector<PeerInfo> get_peers();
    std::vector<NodeId> get_offline_peers() const;
    esp_err_t start_pairing(uint32_t timeout_ms = 30000);

private:
    // --- Notification Bits ---
    static constexpr uint32_t NOTIFY_LOGICAL_ACK     = 0x01;
    static constexpr uint32_t NOTIFY_PHYSICAL_FAIL   = 0x02;
    static constexpr uint32_t NOTIFY_HUB_FOUND       = 0x04;
    static constexpr uint32_t NOTIFY_HEARTBEAT       = 0x08;
    static constexpr uint32_t NOTIFY_PAIRING         = 0x10;
    static constexpr uint32_t NOTIFY_DATA            = 0x20;
    static constexpr uint32_t NOTIFY_ACK_TIMEOUT     = 0x40;
    static constexpr uint32_t NOTIFY_PAIRING_TIMEOUT = 0x80;
    static constexpr uint32_t NOTIFY_STOP            = 0x100;
    static constexpr uint32_t NOTIFY_LINK_ALIVE      = 0x200;

    // --- Private Members ---
    EspNowConfig config_{};

    std::unique_ptr<IPeerManager> peer_manager_;
    std::unique_ptr<ITxStateMachine> tx_state_machine_;
    std::unique_ptr<IChannelScanner> channel_scanner_;
    std::unique_ptr<IMessageCodec> message_codec_;

    SemaphoreHandle_t pairing_mutex_ = nullptr;
    SemaphoreHandle_t ack_mutex_     = nullptr;
    bool is_initialized_             = false;
    std::optional<MessageHeader> last_header_requiring_ack_{};
    bool is_pairing_active_                     = false;
    TimerHandle_t pairing_timer_handle_         = nullptr;
    TimerHandle_t pairing_timeout_timer_handle_ = nullptr;
    TimerHandle_t heartbeat_timer_handle_       = nullptr;
    TimerHandle_t ack_timeout_timer_handle_     = nullptr;

    QueueHandle_t rx_dispatch_queue_           = nullptr;
    QueueHandle_t transport_worker_queue_      = nullptr;
    QueueHandle_t tx_queue_                    = nullptr;
    TaskHandle_t rx_dispatch_task_handle_      = nullptr;
    TaskHandle_t transport_worker_task_handle_ = nullptr;
    TaskHandle_t tx_manager_task_handle_       = nullptr;

    // --- Private Methods ---
    void send_pair_request();
    esp_err_t send_packet(const uint8_t *mac_addr, const void *data, size_t len);
    uint64_t get_time_ms() const;

    // Persistence helpers
    void update_wifi_channel(uint8_t channel);

    // Protocol Message Processing
    void handle_pair_request(const RxPacket &packet);
    void handle_pair_response(const RxPacket &packet);
    void handle_heartbeat(const RxPacket &packet);
    void handle_heartbeat_response(const RxPacket &packet);
    void handle_scan_probe(const RxPacket &packet);
    void send_heartbeat();

    // Task functions
    static void rx_dispatch_task(void *arg);
    static void transport_worker_task(void *arg);
    static void tx_manager_task(void *arg);
    static void pairing_timer_cb(TimerHandle_t xTimer);
    static void periodic_pairing_cb(TimerHandle_t xTimer);
    static void periodic_heartbeat_cb(TimerHandle_t xTimer);

    // Static ESP-NOW callbacks (ISR context)
    static void esp_now_recv_cb(const esp_now_recv_info_t *info,
                                const uint8_t *data,
                                int len);
    static void esp_now_send_cb(const esp_now_send_info_t *info,
                                esp_now_send_status_t status);
};
