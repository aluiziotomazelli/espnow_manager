// include/tx_manager.hpp
#pragma once

#include <atomic>

#include "i_hal_espnow.hpp"
#include "i_hal_freertos.hpp"
#include "i_message_codec.hpp"
#include "i_tx_manager.hpp"
#include "i_tx_state_machine.hpp"
#include "i_statistics_manager.hpp"
#include "i_peer_manager.hpp"

class TxManager : public ITxManager
{
public:
    TxManager(
        ITxStateMachine& fsm,
        IEspNowHAL& hal_espnow,
        IFreeRTOSHAL& freertos_hal,
        IMessageCodec& codec,
        IStatisticsManager& stats_mgr,
        IPeerManager& peer_mgr);

    ~TxManager() override;

    /** @copydoc ITxManager::init */
    esp_err_t init(uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle, uint32_t ack_timeout_ms) override;

    /** @copydoc ITxManager::deinit */
    void deinit() override;

    /** @copydoc ITxManager::queue_packet */
    esp_err_t queue_packet(const DecodedTxPacket& packet) override;

    // Notifications from outside (ISRs or other tasks)
    /** @copydoc ITxManager::notify_delivery */
    void notify_delivery(esp_now_send_status_t status, const uint8_t* dest_mac) override;

    /** @copydoc ITxManager::notify_link_alive */
    void notify_link_alive() override;

    /** @copydoc ITxManager::handle_ack */
    void handle_ack(const DecodedRxPacket& decoded) override;

    /** @copydoc ITxManager::get_task_handle */
    TaskHandle_t get_task_handle() const override { return tx_task_handle_; }

private:
    // Dependencies
    ITxStateMachine& fsm_;
    IEspNowHAL& hal_espnow_;
    IMessageCodec& codec_;
    IFreeRTOSHAL& freertos_hal_;
    IStatisticsManager& stats_mgr_;
    IPeerManager& peer_mgr_;

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
    void notify_logical_ack();

    // FreeRTOS delivery event queue (ISR → tx_task)
    QueueHandle_t delivery_queue_ = nullptr;
};
