// include/tx_manager.hpp
#pragma once

#include "i_discovery_manager.hpp"
#include "i_hal_wifi.hpp"
#include "i_hal_freertos.hpp"
#include "i_message_codec.hpp"
#include "i_tx_manager.hpp"
#include "i_tx_state_machine.hpp"

class TxManager : public ITxManager
{
public:
    TxManager(
        ITxStateMachine &fsm,
        IDiscoveryManager &scanner,
        IWiFiHAL &hal,
        IFreeRTOSHAL &freertos_hal,
        IMessageCodec &codec,
        uint32_t ack_timeout_ms);

    ~TxManager();

    esp_err_t init(uint32_t stack_size, UBaseType_t priority) override;
    esp_err_t deinit() override;

    esp_err_t queue_packet(const DecodedTxPacket &packet) override;

    // Notifications from outside (ISRs or other tasks)
    void notify_physical_fail() override;
    void notify_scanning() override;
    void notify_link_alive() override;
    void notify_logical_ack() override;

    TaskHandle_t get_task_handle() const override { return task_handle_; }

private:
    ITxStateMachine &fsm_;
    IDiscoveryManager &scanner_;
    IWiFiHAL &hal_;
    IMessageCodec &codec_;
    IFreeRTOSHAL &freertos_hal_;

    uint16_t sequence_counter_ = 0;
    SemaphoreHandle_t task_done_semaphore_;
    QueueHandle_t tx_queue_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    TimerHandle_t ack_timeout_timer_ = nullptr;
    uint32_t ack_timeout_ms_;

    static void tx_task_func(void *arg);
    void tx_task();
    static void ack_timeout_callback(TimerHandle_t xTimer);
    void handle_esp_now_send_errors(esp_err_t error);
    void handle_notifications(uint32_t notification, bool &should_stop);
};
