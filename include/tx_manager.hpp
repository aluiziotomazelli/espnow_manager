#pragma once

#include <memory>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "espnow_interfaces.hpp"

class RealTxManager : public ITxManager
{
public:
    RealTxManager(ITxStateMachine &fsm, IChannelScanner &scanner, IWiFiHAL &hal, IMessageCodec &codec);
    ~RealTxManager();

    esp_err_t init(uint32_t stack_size, UBaseType_t priority) override;
    esp_err_t deinit() override;

    esp_err_t queue_packet(const TxPacket &packet) override;

    // Notifications from outside (ISRs or other tasks)
    void notify_physical_fail() override;
    void notify_link_alive() override;
    void notify_logical_ack() override;
    void notify_hub_found() override;

    TaskHandle_t get_task_handle() const override
    {
        return task_handle_;
    }

private:
    ITxStateMachine &fsm_;
    IChannelScanner &scanner_;
    IWiFiHAL &hal_;
    IMessageCodec &codec_;

    QueueHandle_t tx_queue_          = nullptr;
    TaskHandle_t task_handle_        = nullptr;
    TimerHandle_t ack_timeout_timer_ = nullptr;
    uint16_t sequence_counter_       = 0;

    static void tx_task_func(void *arg);
    void run();
};
