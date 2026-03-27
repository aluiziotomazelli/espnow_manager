#pragma once

#include "i_pairing_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_hal_freertos.hpp"

class PairingManager : public IPairingManager
{
public:
    PairingManager(ITxManager& tx_mgr, IPeerManager& peer_mgr, IFreeRTOSHAL& hal_freertos);

    ~PairingManager() = default;

    using IPairingManager::init;

    esp_err_t init(NodeId id, NodeType type, TaskHandle_t rx_task_handle) override;

    void tick(uint64_t now_ms) override;

    esp_err_t start(uint32_t timeout_ms, uint64_t now_ms) override;

    void handle_request(const DecodedPacket& decoded) override;
    void handle_response(const DecodedPacket& decoded) override;

protected:
    void send_pair_request();
    void on_timeout();

private:
    ITxManager& tx_mgr_;
    IPeerManager& peer_mgr_;
    IFreeRTOSHAL& hal_freertos_;

    NodeType my_type_;
    NodeId my_id_;

    bool is_initialized_ = false;
    bool is_active_ = false;
    uint8_t current_channel_ = 1;

    TaskHandle_t rx_task_handle_;

    uint64_t started_at_ms_ = 0;
    uint64_t last_request_ms_ = 0;
    uint32_t timeout_ms_ = PAIRING_TIMEOUT_MS;
    static constexpr uint32_t periodic_interval_ms_ = PAIRING_PERIODIC_INTERVAL_MS;

    void notify_rx_task_pairing_done();
};
