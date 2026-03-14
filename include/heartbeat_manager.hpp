#pragma once

#include "i_heartbeat_manager.hpp"
#include "i_message_codec.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_hal_freertos.hpp"
#include "i_hal_timer.hpp"

class HeartbeatManager : public IHeartbeatManager
{
public:
    HeartbeatManager(
        NodeId my_id,
        ITxManager &tx_mgr,
        IPeerManager &peer_mgr,
        IMessageCodec &codec,
        IFreeRTOSHAL &hal_freertos,
        ITimerHAL &hal_timer);
    ~HeartbeatManager();

    using IHeartbeatManager::handle_request;
    using IHeartbeatManager::handle_response;
    using IHeartbeatManager::init;
    using IHeartbeatManager::update_node_id;

    esp_err_t init(uint32_t interval_ms, NodeType type) override;
    void update_node_id(NodeId id) override;
    esp_err_t deinit() override;
    void handle_response(NodeId hub_id) override;
    void handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms) override;

private:
    NodeId my_id_;

    ITxManager &tx_mgr_;
    IPeerManager &peer_mgr_;
    IMessageCodec &codec_;
    IFreeRTOSHAL &hal_freertos_;
    ITimerHAL &hal_timer_;

    NodeType my_type_;
    uint32_t interval_ms_;
    TimerHandle_t timer_ = nullptr;

protected:
    void send_heartbeat();

private:
    static void timer_cb(TimerHandle_t xTimer);
};
