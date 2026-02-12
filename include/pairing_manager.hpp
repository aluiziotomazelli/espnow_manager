#pragma once

#include "espnow_interfaces.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

class RealPairingManager : public IPairingManager
{
public:
    RealPairingManager(ITxManager &tx_mgr, IPeerManager &peer_mgr, IMessageCodec &codec);
    ~RealPairingManager();

    esp_err_t init(NodeType type, NodeId id) override;
    esp_err_t deinit() override;
    esp_err_t start(uint32_t timeout_ms) override;
    bool is_active() const override { return is_active_; }
    void handle_request(const RxPacket &packet) override;
    void handle_response(const RxPacket &packet) override;

private:
    ITxManager &tx_mgr_;
    IPeerManager &peer_mgr_;
    IMessageCodec &codec_;
    NodeType my_type_;
    NodeId my_id_;
    bool is_active_ = false;
    TimerHandle_t timeout_timer_ = nullptr;
    TimerHandle_t periodic_timer_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;

    void send_pair_request();
    static void timeout_cb(TimerHandle_t xTimer);
    static void periodic_cb(TimerHandle_t xTimer);
    void on_timeout();
};
