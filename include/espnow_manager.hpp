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

// Main class for ESP-NOW communication.
class EspNowManager : public IEspNowManager
{
public:
    // Singleton
    static EspNowManager &instance();

    // Dependency injection constructor for testing
    EspNowManager(std::unique_ptr<IPeerManager> peer_manager,
                  std::unique_ptr<ITxManager> tx_manager,
                  IChannelScanner *scanner_ptr,
                  std::unique_ptr<IMessageCodec> message_codec,
                  std::unique_ptr<IHeartbeatManager> heartbeat_manager,
                  std::unique_ptr<IPairingManager> pairing_manager,
                  std::unique_ptr<IMessageRouter> message_router);

    EspNowManager(const EspNowManager &)            = delete;
    EspNowManager &operator=(const EspNowManager &) = delete;
    virtual ~EspNowManager();

    // Public API
    esp_err_t init(const EspNowConfig &config) override;
    esp_err_t deinit() override;

    using IEspNowManager::send_data;
    esp_err_t send_data(NodeId dest_node_id,
                        PayloadType payload_type,
                        const void *payload,
                        size_t len,
                        bool require_ack = false) override;

    using IEspNowManager::send_command;
    esp_err_t send_command(NodeId dest_node_id,
                           CommandType command_type,
                           const void *payload,
                           size_t len,
                           bool require_ack = false) override;

    esp_err_t confirm_reception(AckStatus status) override;

    // Peer Management Functions
    using IEspNowManager::add_peer;
    esp_err_t add_peer(NodeId node_id, const uint8_t *mac, uint8_t channel, NodeType type) override;

    using IEspNowManager::remove_peer;
    esp_err_t remove_peer(NodeId node_id) override;

    std::vector<PeerInfo> get_peers() override;
    std::vector<NodeId> get_offline_peers() const override;
    esp_err_t start_pairing(uint32_t timeout_ms = 30000) override;

    bool is_initialized() const override
    {
        return is_initialized_;
    }

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
