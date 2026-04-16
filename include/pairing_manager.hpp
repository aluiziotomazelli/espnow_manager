#pragma once

#include "i_pairing_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_hal_freertos.hpp"
#include "i_hal_timer.hpp"

class PairingManager : public IPairingManager
{
public:
    PairingManager(ITxManager& tx_mgr, IPeerManager& peer_mgr, IFreeRTOSHAL& hal_freertos, ITimerHAL& hal_timer);

    ~PairingManager() = default;

    using IPairingManager::init;

    /** @copydoc IPairingManager::init */
    esp_err_t init(NodeId id, NodeType type, TaskHandle_t rx_task_handle, uint32_t heartbeat_interval_ms) override;

    /** @copydoc IPairingManager::deinit */
    void deinit() override;

    /** @copydoc IPairingManager::tick */
    void tick(int64_t now_ms) override;

    /** @copydoc IPairingManager::start */
    esp_err_t start(uint32_t timeout_ms, int64_t now_ms) override;

    /** @copydoc IPairingManager::handle_request */
    void handle_request(const DecodedRxPacket& decoded) override;

    /** @copydoc IPairingManager::handle_response */
    void handle_response(const DecodedRxPacket& decoded) override;

protected:
    void send_pair_request();
    void on_timeout();
    bool is_active_ = false;

private:
    ITxManager& tx_mgr_;
    IPeerManager& peer_mgr_;
    IFreeRTOSHAL& hal_freertos_;
    ITimerHAL& hal_timer_;

    NodeType my_type_;
    NodeId my_id_;

    bool is_initialized_ = false;
    uint8_t current_channel_ = 1;

    TaskHandle_t rx_task_handle_;
    uint32_t heartbeat_interval_ms_ = 60000;

    int64_t started_at_ms_ = 0;
    int64_t last_request_ms_ = 0;
    uint32_t timeout_ms_ = PAIRING_TIMEOUT_MS;
    static constexpr uint32_t periodic_interval_ms_ = PAIRING_PERIODIC_INTERVAL_MS;

    void notify_rx_task_pairing_done();
    void notify_rx_task_peer_add();
    int64_t get_time_ms() const;
};
