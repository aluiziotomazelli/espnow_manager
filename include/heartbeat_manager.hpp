#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "i_heartbeat_manager.hpp"
#include "i_message_codec.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"

class HeartbeatManager : public IHeartbeatManager
{
public:
    HeartbeatManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, IMessageCodec &codec, NodeId my_id);
    ~HeartbeatManager();

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

protected:
    void send_heartbeat();

private:
    static void timer_cb(TimerHandle_t xTimer);
};
