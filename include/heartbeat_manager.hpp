#pragma once

#include "espnow_interfaces.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

class RealHeartbeatManager : public IHeartbeatManager
{
public:
    RealHeartbeatManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, IMessageCodec &codec, NodeId my_id);
    ~RealHeartbeatManager();

    using IHeartbeatManager::handle_request;
    using IHeartbeatManager::handle_response;
    using IHeartbeatManager::init;
    using IHeartbeatManager::update_node_id;

    esp_err_t init(uint32_t interval_ms, NodeType type) override;
    void update_node_id(NodeId id) override;
    esp_err_t deinit() override;
    void handle_response(NodeId hub_id, uint8_t channel) override;
    void handle_request(NodeId sender_id, const uint8_t *mac, uint64_t uptime_ms) override;

private:
    ITxManager &tx_mgr_;
    IPeerManager &peer_mgr_;
    IMessageCodec &codec_;
    NodeId my_id_;
    NodeType my_type_;
    uint32_t interval_ms_;
    TimerHandle_t timer_ = nullptr;

    void send_heartbeat();
    static void timer_cb(TimerHandle_t xTimer);
};
