#pragma once

#include "i_heartbeat_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_en_hal_freertos.hpp"
#include "i_en_hal_timer.hpp"
namespace espnow {

class HeartbeatManager : public IHeartbeatManager
{
public:
    HeartbeatManager(ITxManager& tx_mgr, IPeerManager& peer_mgr, ITimerHAL& hal_timer);
    ~HeartbeatManager() = default;

    using IHeartbeatManager::handle_request;
    using IHeartbeatManager::handle_response;
    using IHeartbeatManager::init;

    /** @copydoc IHeartbeatManager::init */
    void init(NodeId id, NodeType type, uint32_t interval_ms, bool enable_heartbeat = true) override;

    /** @copydoc IHeartbeatManager::deinit */
    void deinit() override;

    /** @copydoc IHeartbeatManager::tick */
    void tick(int64_t now_ms) override;

    /** @copydoc IHeartbeatManager::set_interval_ms */
    void set_interval_ms(uint32_t heartbeat_interval_ms) override;

    /** @copydoc IHeartbeatManager::set_enable_heartbeat */
    void set_enable_heartbeat(bool enable) override;

    /** @copydoc IHeartbeatManager::is_heartbeat_enabled */
    bool is_heartbeat_enabled() const override;

    /** @copydoc IHeartbeatManager::handle_response */
    void handle_response(const DecodedRxPacket& decoded) override;

    /** @copydoc IHeartbeatManager::handle_request */
    void handle_request(const DecodedRxPacket& decoded) override;

protected:
    void send_heartbeat();

private:
    int64_t get_time_ms() const;

    NodeId my_id_;

    ITxManager& tx_mgr_;
    IPeerManager& peer_mgr_;
    ITimerHAL& hal_timer_;

    NodeType my_type_;
    uint32_t interval_ms_;
    bool enable_heartbeat_ = true;
    int8_t last_rssi_ = 0; /**< RSSI of the Hub as seen by this Node */

    bool is_initialized_ = false;
    int64_t last_heartbeat_ms_ = 0;
};

} // namespace espnow
