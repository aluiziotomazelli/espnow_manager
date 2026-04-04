// include/tx_manager.hpp
#pragma once

#include <atomic>

#include "i_hal_espnow.hpp"
#include "i_hal_freertos.hpp"
#include "i_message_codec.hpp"
#include "i_tx_manager.hpp"
#include "i_tx_state_machine.hpp"

class TxManager : public ITxManager
{
public:
    TxManager(
        ITxStateMachine& fsm,
        IEspNowHAL& hal_espnow,
        IFreeRTOSHAL& freertos_hal,
        IMessageCodec& codec,
        uint32_t ack_timeout_ms);

    ~TxManager() override;

    /** @copydoc ITxManager::init */
    esp_err_t init(uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle) override;

    /** @copydoc ITxManager::deinit */
    void deinit() override;

    /** @copydoc ITxManager::queue_packet */
    esp_err_t queue_packet(const DecodedTxPacket& packet) override;

    // Notifications from outside (ISRs or other tasks)
    /** @copydoc ITxManager::notify_delivery_failure */
    void notify_delivery_failure() override;

    /** @copydoc ITxManager::notify_delivery_success */
    void notify_delivery_success() override;

    /** @copydoc ITxManager::notify_link_alive */
    void notify_link_alive() override;

    /** @copydoc ITxManager::handle_ack */
    void handle_ack(const DecodedRxPacket& decoded) override;

    /** @copydoc ITxManager::get_task_handle */
    TaskHandle_t get_task_handle() const override { return tx_task_handle_; }

private:
    // Internal notification (called by handle_ack after validation)
    /** @copydoc ITxManager::notify_logical_ack */
    void notify_logical_ack() override;

    // Dependencies
    ITxStateMachine& fsm_;
    IEspNowHAL& hal_espnow_;
    IMessageCodec& codec_;
    IFreeRTOSHAL& freertos_hal_;

    uint16_t sequence_counter_ = 0;
    uint32_t ack_timeout_ms_;
    TaskHandle_t rx_task_handle_ = nullptr;

    // FreeRTOS resources
    SemaphoreHandle_t task_done_semaphore_;
    QueueHandle_t tx_queue_ = nullptr;
    TimerHandle_t ack_timeout_timer_ = nullptr;

    // Task related
    TaskHandle_t tx_task_handle_ = nullptr;
    static void tx_task_func(void* arg);
    void tx_task();

    // Timer callback
    static void ack_timeout_callback(TimerHandle_t xTimer);

    // Helper methods
    void handle_esp_now_send_errors(esp_err_t error);
    void handle_notifications(uint32_t notification, bool& should_stop);
};
