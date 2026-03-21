#pragma once

#include "i_heartbeat_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_hal_freertos.hpp"
#include "i_hal_timer.hpp"

class HeartbeatManager : public IHeartbeatManager
{
public:
    HeartbeatManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, ITimerHAL &hal_timer);
    ~HeartbeatManager() = default;

    using IHeartbeatManager::handle_request;
    using IHeartbeatManager::handle_response;
    using IHeartbeatManager::init;

    void init(NodeId id, NodeType type, uint32_t interval_ms) override;
    void tick(uint64_t now_ms) override;
    void set_interval_ms(uint32_t heartbeat_interval_ms) override;
    void handle_response() override;
    void handle_request(const DecodedPacket &decoded) override;

protected:
    void send_heartbeat();

private:
    NodeId my_id_;

    ITxManager &tx_mgr_;
    IPeerManager &peer_mgr_;
    ITimerHAL &hal_timer_;

    NodeType my_type_;
    uint32_t interval_ms_;

    bool is_initialized_ = false;
    uint64_t last_heartbeat_ms_ = 0;
};
