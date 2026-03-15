#pragma once

#include "i_pairing_manager.hpp"
#include "i_tx_manager.hpp"
#include "i_peer_manager.hpp"
#include "i_message_codec.hpp"
#include "i_hal_freertos.hpp"

class PairingManager : public IPairingManager
{
public:
    PairingManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, IMessageCodec &codec, IFreeRTOSHAL &hal_freertos);
    ~PairingManager();

    using IPairingManager::init;

    esp_err_t init(NodeType type, NodeId id) override;
    esp_err_t deinit() override;
    esp_err_t start(uint32_t timeout_ms) override;
    void set_channel(uint8_t channel) override;
    bool is_active() const override
    {
        return is_active_;
    }
    void handle_request(const RxPacket &packet) override;
    void handle_response(const RxPacket &packet) override;

protected:
    void send_pair_request();
    void on_timeout();

private:
    ITxManager &tx_mgr_;
    IPeerManager &peer_mgr_;
    IMessageCodec &codec_;
    IFreeRTOSHAL &hal_freertos_;

    NodeType my_type_;
    NodeId my_id_;
    bool is_active_               = false;
    uint8_t current_channel_      = 1;
    TimerHandle_t timeout_timer_  = nullptr;
    TimerHandle_t periodic_timer_ = nullptr;
    SemaphoreHandle_t mutex_      = nullptr;

    static void timeout_cb(TimerHandle_t xTimer);
    static void periodic_cb(TimerHandle_t xTimer);
};
