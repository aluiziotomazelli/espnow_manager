// include/espnow_manager.hpp
#pragma once

#include <memory>
#include <atomic>

#include "etl/vector.h"

// #include "freertos/FreeRTOS.h"
// #include "freertos/queue.h"
// #include "freertos/semphr.h"
// #include "freertos/task.h"

#include "i_discovery_manager.hpp"
#include "i_espnow_manager.hpp"
#include "i_heartbeat_manager.hpp"
#include "i_message_codec.hpp"
#include "i_message_router.hpp"
#include "i_pairing_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_hal_timer.hpp"
#include "i_tx_manager.hpp"
#include "i_tx_state_machine.hpp"
// #include "i_hal_nvs.hpp"
#include "i_hal_wifi.hpp"
#include "i_espnow_driver.hpp"
#include "i_hal_freertos.hpp"
// #include "storage_manager.hpp"

// ========================================
// ESP-NOW Manager Implementation
// ========================================

/**
 * @class EspNowManager
 * @brief Implementation of IEspNowManager for ESP32 using ESP-NOW.
 *
 * @see IEspNowManager for full API documentation.
 */
class EspNowManager : public IEspNowManager, public IChannelObserver // Modified inheritance
{
public:
    /** @brief Get the singleton instance of EspNowManager */
    static EspNowManager &instance();

    /**
     * @brief Dependency injection constructor for testing
     * @internal
     */
    EspNowManager(
        std::unique_ptr<IWiFiHAL> hal_wifi,
        std::unique_ptr<ITimerHAL> hal_timer,
        std::unique_ptr<IFreeRTOSHAL> hal_freertos,
        std::unique_ptr<IEspNowDriver> espnow_driver,
        std::unique_ptr<IPeerManager> peer_manager,
        std::unique_ptr<IMessageCodec> message_codec,
        std::unique_ptr<IDiscoveryManager> scanner,
        std::unique_ptr<ITxStateMachine> tx_fsm,
        std::unique_ptr<ITxManager> tx_manager,
        std::unique_ptr<IHeartbeatManager> heartbeat_manager,
        std::unique_ptr<IPairingManager> pairing_manager,
        std::unique_ptr<IMessageRouter> message_router);

    EspNowManager(const EspNowManager &) = delete;
    EspNowManager &operator=(const EspNowManager &) = delete;
    virtual ~EspNowManager();

    // ========================================
    // Lifecycle
    // ========================================

    /** @copydoc IEspNowManager::init */
    esp_err_t init(const EspNowConfig &config) override;

    /** @copydoc IEspNowManager::deinit */
    esp_err_t deinit() override;

    // ========================================
    // Data Communication
    // ========================================

    using IEspNowManager::send_data;
    /** @copydoc IEspNowManager::send_data */
    esp_err_t
    send_data(NodeId dest_node_id, PayloadType payload_type, const void *payload, size_t len, bool require_ack = false)
        override;

    using IEspNowManager::send_command;
    /** @copydoc IEspNowManager::send_command */
    esp_err_t send_command(
        NodeId dest_node_id,
        CommandType command_type,
        const void *payload,
        size_t len,
        bool require_ack = false) override;

    /** @copydoc IEspNowManager::confirm_reception */
    esp_err_t confirm_reception(AckStatus status) override;

    // ========================================
    // Peer Management
    // ========================================

    using IEspNowManager::add_peer;
    /** @copydoc IEspNowManager::add_peer */
    esp_err_t add_peer(NodeId node_id, const uint8_t *mac, NodeType type, uint32_t heartbeat_interval_ms) override;

    using IEspNowManager::remove_peer;
    /** @copydoc IEspNowManager::remove_peer */
    esp_err_t remove_peer(NodeId node_id) override;

    /** @copydoc IEspNowManager::get_peers */
    etl::vector<PeerInfo, MAX_PEERS> get_peers() override;

    /** @copydoc IEspNowManager::get_offline_peers */
    etl::vector<NodeId, MAX_PEERS> get_offline_peers() const override;

    // ========================================
    // Pairing
    // ========================================

    /** @copydoc IEspNowManager::start_pairing */
    esp_err_t start_pairing(uint32_t timeout_ms = 30000) override;

    // ========================================
    // Status
    // ========================================

    /** @copydoc IEspNowManager::is_initialized */
    bool is_initialized() const override;

    /** @copydoc IEspNowManager::get_node_state */
    NodeState get_node_state() const override;

protected:
    // --- Notification Bits ---
    // static constexpr uint32_t NOTIFY_STOP = 0x100;

    // --- Private Members ---
    EspNowConfig config_{};

    // --- Sub-components (Interfaces) ---
    std::unique_ptr<IWiFiHAL> hal_wifi_;                   ///< Pointer to WiFi HAL
    std::unique_ptr<ITimerHAL> hal_timer_;                 ///< Pointer to timer HAL
    std::unique_ptr<IFreeRTOSHAL> hal_freertos_;           ///< Pointer to FreeRTOS HAL
    std::unique_ptr<IEspNowDriver> espnow_driver_;         ///< Pointer to espnow_driver
    std::unique_ptr<IPeerManager> peer_manager_;           ///< Pointer to peer manager
    std::unique_ptr<IMessageCodec> message_codec_;         ///< Pointer to message codec
    std::unique_ptr<IDiscoveryManager> scanner_;           ///< Pointer to discovery manager
    std::unique_ptr<ITxStateMachine> tx_fsm_;              ///< Pointer to tx state machine
    std::unique_ptr<ITxManager> tx_manager_;               ///< Pointer to tx manager
    std::unique_ptr<IHeartbeatManager> heartbeat_manager_; ///< Pointer to heartbeat manager
    std::unique_ptr<IPairingManager> pairing_manager_;     ///< Pointer to pairing manager
    std::unique_ptr<IMessageRouter> message_router_;       ///< Pointer to message router

    SemaphoreHandle_t ack_mutex_ = nullptr;
    bool esp_now_initialized_ = false;
    std::optional<MessageHeader> last_header_requiring_ack_{};

    QueueHandle_t rx_dispatch_queue_ = nullptr;
    TaskHandle_t rx_task_handle_ = nullptr;

    /** @brief IChannelObserver implementation */
    void on_channel_found_cb(uint8_t channel) override;
    void on_scan_failed_cb() override;
    void on_scan_started_cb() override;

    // --- Private Methods ---
    uint64_t get_time_ms() const;

    // Send packet helper
    esp_err_t send_packet(
        NodeId dest_node_id,
        MessageType msg_type,
        PayloadType payload_type,
        const void *payload,
        size_t len,
        bool require_ack);

    // Persistence helpers
    void update_wifi_channel(uint8_t channel);
    void propagate_channel();

    // Init helpers
    esp_err_t create_mutex();
    esp_err_t create_queues();
    esp_err_t create_tasks();
    esp_err_t init_tx_manager();
    esp_err_t init_discovery_manager();
    esp_err_t init_heartbeat_manager();
    esp_err_t init_pairing_manager();
    NodeState determine_initial_state(etl::ivector<PeerInfo> &peers);
    void add_peers_to_espnow(etl::ivector<PeerInfo> &peers);
    esp_err_t init_fail(esp_err_t ret, const char *step);

    void signal_tasks_to_stop();
    void delete_tasks();
    void cleanup_resources();

    // Task functions
    static void rx_task(void *arg);

    bool is_protocol_message(MessageType type);
    void handle_notifications(uint32_t notifications, bool &should_stop);
    static AppMessage build_app_message(const DecodedPacket &decoded);

    // Static ESP-NOW callbacks (ISR context)
    static void esp_now_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void esp_now_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status);

    std::atomic<uint8_t> last_found_channel_{0};
    std::atomic<NodeState> node_state_{NodeState::UNINITIALIZED};

    // Store pairing timeout for use after channel scan completes
    uint32_t pairing_timeout_ms_ = PAIRING_TIMEOUT_MS;

    void transition_to_state(NodeState new_state);
};
