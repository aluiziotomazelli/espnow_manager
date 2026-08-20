// include/espnow_manager.hpp
#pragma once

#include <memory>
#include <atomic>
#include "etl/vector.h"

#include "i_discovery_manager.hpp"
#include "i_espnow_manager.hpp"
#include "i_heartbeat_manager.hpp"
#include "i_message_codec.hpp"
#include "i_message_router.hpp"
#include "i_node_state_machine.hpp"
#include "i_pairing_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_en_hal_timer.hpp"
#include "i_tx_manager.hpp"
#include "i_tx_state_machine.hpp"
#include "i_en_hal_wifi.hpp"
#include "i_en_hal_espnow.hpp"
#include "i_espnow_driver.hpp"
#include "i_en_hal_freertos.hpp"
#include "i_storage_manager.hpp"
#include "i_channel_monitor.hpp"
#include "i_statistics_manager.hpp"

namespace espnow {

// ========================================
// ESP-NOW Manager Implementation
// ========================================

/**
 * @class EspNowManager
 * @brief Implementation of IEspNowManager for ESP32 using ESP-NOW.
 *
 * @see IEspNowManager for full API documentation.
 */
class EspNowManager : public IEspNowManager
{
public:
    /** @brief Get the singleton instance of EspNowManager */
    static EspNowManager& instance();

    /**
     * @brief Dependency injection constructor for testing
     * @internal
     */
    EspNowManager(
        std::unique_ptr<IStorageManager> storage,
        std::unique_ptr<IWiFiHAL> hal_wifi,
        std::unique_ptr<ITimerHAL> hal_timer,
        std::unique_ptr<IFreeRTOSHAL> hal_freertos,
        std::unique_ptr<IEspNowHAL> hal_espnow,
        std::unique_ptr<IEspNowDriver> espnow_driver,
        std::unique_ptr<IPeerManager> peer_manager,
        std::unique_ptr<IMessageCodec> message_codec,
        std::unique_ptr<IChannelMonitor> channel_monitor,
        std::unique_ptr<IDiscoveryManager> scanner,
        std::unique_ptr<ITxStateMachine> tx_fsm,
        std::unique_ptr<ITxManager> tx_manager,
        std::unique_ptr<IHeartbeatManager> heartbeat_manager,
        std::unique_ptr<IPairingManager> pairing_manager,
        std::unique_ptr<IMessageRouter> message_router,
        std::unique_ptr<IStatisticsManager> stats_mgr,
        std::unique_ptr<INodeStateMachine> node_fsm);

    EspNowManager(const EspNowManager&) = delete;
    EspNowManager& operator=(const EspNowManager&) = delete;
    virtual ~EspNowManager();

    // ========================================
    // Lifecycle
    // ========================================

    /** @copydoc IEspNowManager::init */
    esp_err_t init(const EspNowConfig& config) override;

    /** @copydoc IEspNowManager::deinit */
    void deinit() override;

    /** @copydoc IEspNowManager::set_channel_policy */
    void set_channel_policy(ChannelPolicy policy) override;

    // ========================================
    // Data Communication
    // ========================================

    using IEspNowManager::send_data;
    /** @copydoc IEspNowManager::send_data */
    esp_err_t
    send_data(NodeId dest_node_id, PayloadType payload_type, const void* payload, size_t len, bool require_ack = false)
        override;

    using IEspNowManager::send_command;
    /** @copydoc IEspNowManager::send_command */
    esp_err_t send_command(
        NodeId dest_node_id,
        CommandType command_type,
        const void* payload,
        size_t len,
        bool require_ack = false) override;

    /** @copydoc IEspNowManager::confirm_reception */
    esp_err_t confirm_reception(NodeId sender_id, uint16_t sequence_number, AckStatus status) override;

    // ========================================
    // Peer Management
    // ========================================

    using IEspNowManager::add_peer;
    /** @copydoc IEspNowManager::add_peer */
    esp_err_t add_peer(NodeId node_id, const uint8_t* mac, NodeType type, uint32_t heartbeat_interval_ms) override;

    using IEspNowManager::remove_peer;
    /** @copydoc IEspNowManager::remove_peer */
    esp_err_t remove_peer(NodeId node_id) override;

    /** @copydoc IEspNowManager::get_peers */
    etl::vector<PeerInfo, MAX_PEERS> get_peers() override;

    /** @copydoc IEspNowManager::get_offline_peers */
    etl::vector<NodeId, MAX_PEERS> get_offline_peers() const override;

    /** @copydoc IEspNowManager::is_peer_online */
    bool is_peer_online(NodeId node_id) const override;

    // ========================================
    // Pairing
    // ========================================

    /** @copydoc IEspNowManager::start_pairing */
    esp_err_t start_pairing(uint32_t timeout_ms = 30000) override;

    // ========================================
    // Heartbeat Control
    // ========================================

    /** @copydoc IEspNowManager::set_enable_heartbeat */
    void set_enable_heartbeat(bool enable) override;

    /** @copydoc IEspNowManager::is_heartbeat_enabled */
    bool is_heartbeat_enabled() const override;

    /** @copydoc IEspNowManager::set_heartbeat_interval_ms */
    void set_heartbeat_interval_ms(uint32_t interval_ms) override;

    // ========================================
    // Status
    // ========================================

    /** @copydoc IEspNowManager::get_node_state */
    NodeState get_node_state() const override;

    /** @copydoc IEspNowManager::is_initialized */
    bool is_initialized() const override;

    /** @copydoc IEspNowManager::reconnect */
    esp_err_t reconnect() override;

    // ========================================
    // Statistics
    // ========================================

    /** @copydoc IEspNowManager::get_peer_stats */
    bool get_peer_stats(NodeId node_id, PeerStatistics& out) const override;

    /** @copydoc IEspNowManager::get_all_peer_stats */
    etl::vector<PeerStatistics, MAX_PEERS> get_all_peer_stats() const override;

protected:
    // --- Private Members ---
    EspNowConfig config_{};

    // --- Sub-components (Interfaces) ---
    std::unique_ptr<IStorageManager> storage_;             ///< Pointer to storage
    std::unique_ptr<IWiFiHAL> hal_wifi_;                   ///< Pointer to WiFi HAL
    std::unique_ptr<ITimerHAL> hal_timer_;                 ///< Pointer to timer HAL
    std::unique_ptr<IFreeRTOSHAL> hal_freertos_;           ///< Pointer to FreeRTOS HAL
    std::unique_ptr<IEspNowHAL> hal_espnow_;               ///< Pointer to ESP-NOW HAL
    std::unique_ptr<IEspNowDriver> espnow_driver_;         ///< Pointer to espnow_driver
    std::unique_ptr<IPeerManager> peer_manager_;           ///< Pointer to peer manager
    std::unique_ptr<IMessageCodec> message_codec_;         ///< Pointer to message codec
    std::unique_ptr<IChannelMonitor> channel_monitor_;     ///< Pointer to channel monitor
    std::unique_ptr<IDiscoveryManager> scanner_;           ///< Pointer to discovery manager
    std::unique_ptr<ITxStateMachine> tx_fsm_;              ///< Pointer to tx state machine
    std::unique_ptr<ITxManager> tx_manager_;               ///< Pointer to tx manager
    std::unique_ptr<IHeartbeatManager> heartbeat_manager_; ///< Pointer to heartbeat manager
    std::unique_ptr<IPairingManager> pairing_manager_;     ///< Pointer to pairing manager
    std::unique_ptr<IMessageRouter> message_router_;       ///< Pointer to message router
    std::unique_ptr<IStatisticsManager> stats_mgr_;        ///< Pointer to statistics manager
    std::unique_ptr<INodeStateMachine> node_fsm_;          ///< Pointer to node state machine

    bool esp_now_initialized_ = false;

    QueueHandle_t rx_queue_handle_ = nullptr;
    TaskHandle_t rx_task_handle_ = nullptr;

    // --- Private Methods ---
    int64_t get_time_ms() const;

    // Send packet helper
    esp_err_t send_packet(
        NodeId dest_node_id,
        MessageType msg_type,
        PayloadType payload_type,
        const void* payload,
        size_t len,
        bool require_ack);

    // Persistence helpers
    void update_wifi_channel(uint8_t channel);

    // Init helpers
    esp_err_t create_queue();
    esp_err_t create_task();
    esp_err_t init_tx_manager();
    esp_err_t init_discovery_manager();
    esp_err_t init_heartbeat_manager();
    esp_err_t init_pairing_manager();
    esp_err_t init_channel_monitor();
    void add_peers_to_espnow(etl::ivector<PeerInfo>& peers);
    esp_err_t init_fail(esp_err_t ret, const char* step);

    void signal_task_to_stop();
    void delete_task();
    void cleanup_resources();

    // Task functions
    static void rx_task(void* arg);

    void handle_notifications(uint32_t notifications, bool& should_stop); // protected
    void handle_state_transition(NodeState old_state, NodeState new_state);
    static AppMessage build_app_message(const DecodedRxPacket& decoded);

    // Static ESP-NOW callbacks (ISR context)
    static void esp_now_recv_cb(const esp_now_recv_info_t* info, const uint8_t* data, int len);
    static void esp_now_send_cb(const esp_now_send_info_t* info, esp_now_send_status_t status);

    std::atomic<uint8_t> last_found_channel_{0};

    // Store pairing timeout for use after channel scan completes
    uint32_t pairing_timeout_ms_ = PAIRING_TIMEOUT_MS;

    struct ScanRetry
    {
        bool active = false;
        int count = 0;
        int64_t next_attempt_ms = 0;

        void reset()
        {
            active = false;
            count = 0;
            next_attempt_ms = 0;
        }
    };

    ScanRetry scan_retry_{};

    void tick_scan_retry(int64_t now_ms); // protected
    void handle_scan_retries(bool has_peers);
};

} // namespace espnow
