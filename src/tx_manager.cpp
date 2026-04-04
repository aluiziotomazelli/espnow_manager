#include <cstring>

#include "esp_log.h"

#include "tx_manager.hpp"

static const char* TAG = "TxManager";

TxManager::TxManager(
    ITxStateMachine& fsm,
    IEspNowHAL& hal_espnow,
    IFreeRTOSHAL& freertos_hal,
    IMessageCodec& codec,
    uint32_t ack_timeout_ms)
    : fsm_(fsm)
    , hal_espnow_(hal_espnow)
    , codec_(codec)
    , freertos_hal_(freertos_hal)
    , sequence_counter_(0)
    , ack_timeout_ms_(ack_timeout_ms)
    , task_done_semaphore_(nullptr)
    , tx_queue_(nullptr)
    , ack_timeout_timer_(nullptr)
    , tx_task_handle_(nullptr)
{
}

TxManager::~TxManager()
{
    // deinit(); must be called before destruction to clean up resources
}

void TxManager::ack_timeout_callback(TimerHandle_t xTimer)
{
    TxManager* self = static_cast<TxManager*>(pvTimerGetTimerID(xTimer));
    self->freertos_hal_.task_notify(self->tx_task_handle_, NOTIFY_ACK_TIMEOUT, eSetBits);
}

esp_err_t TxManager::init(uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle)
{
    if (rx_task_handle == nullptr) {
        ESP_LOGE(TAG, "RX task handle is null");
        return ESP_ERR_INVALID_ARG;
    }
    rx_task_handle_ = rx_task_handle;

    tx_queue_ = freertos_hal_.queue_create(20, sizeof(DecodedTxPacket));
    if (tx_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    task_done_semaphore_ = freertos_hal_.semaphore_create_binary();
    if (task_done_semaphore_ == nullptr) {
        deinit();
        return ESP_ERR_NO_MEM;
    }

    ack_timeout_timer_ =
        freertos_hal_.timer_create("ack_timeout", pdMS_TO_TICKS(ack_timeout_ms_), pdFALSE, this, ack_timeout_callback);
    if (ack_timeout_timer_ == nullptr) {
        deinit();
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_creation =
        freertos_hal_.task_create(tx_task_func, "tx_manager_task", stack_size, this, priority, &tx_task_handle_);
    if (task_creation != pdPASS) {
        deinit();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void TxManager::deinit()
{
    if (tx_task_handle_ != nullptr) {
        // Notify task to stop
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_TASK_TO_STOP, eSetBits);

        // Send dummy packet to wakeup task
        DecodedTxPacket stop_packet = {};
        if (tx_queue_ != nullptr) {
            freertos_hal_.queue_send(tx_queue_, &stop_packet, 0);
        }

        // Wait for task to exit
        uint8_t delay_ms = 10;
        for (int timeout = 1000; timeout > 0; timeout -= delay_ms) {
            if (freertos_hal_.semaphore_take(task_done_semaphore_, delay_ms) == pdPASS)
                break;
        }
        // Forcing deleting task
        if (tx_task_handle_ != nullptr) {
            ESP_LOGW(TAG, "Forcing deletion of tx manager task");
            freertos_hal_.task_delete(tx_task_handle_);
            tx_task_handle_ = nullptr;
        }
    }

    if (task_done_semaphore_ != nullptr) {
        freertos_hal_.semaphore_delete(task_done_semaphore_);
        task_done_semaphore_ = nullptr;
    }

    if (tx_queue_ != nullptr) {
        freertos_hal_.queue_delete(tx_queue_);
        tx_queue_ = nullptr;
    }

    if (ack_timeout_timer_ != nullptr) {
        freertos_hal_.timer_delete(ack_timeout_timer_, portMAX_DELAY);
        ack_timeout_timer_ = nullptr;
    }
}

esp_err_t TxManager::queue_packet(const DecodedTxPacket& packet)
{
    if (tx_queue_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (freertos_hal_.queue_send(tx_queue_, &packet, 100) != pdTRUE) {
        return ESP_FAIL;
    }

    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_DATA, eSetBits);
    }

    return ESP_OK;
}

void TxManager::notify_delivery_failure()
{
    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_DELIVERY_FAILURE, eSetBits);
    }
}

void TxManager::notify_delivery_success()
{
    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_DELIVERY_SUCCESS, eSetBits);
    }
}

void TxManager::notify_link_alive()
{
    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_LINK_ALIVE, eSetBits);
    }
}

void TxManager::handle_ack(const DecodedRxPacket& decoded)
{
    auto pending_ack = fsm_.get_pending_ack();
    if (!pending_ack.has_value()) {
        ESP_LOGW(TAG, "ACK received but no pending packet");
        return;
    }

    if (decoded.header.sequence_number != pending_ack->sequence_number) {
        ESP_LOGW(
            TAG,
            "ACK seq_num mismatch, expected: %d, got: %d",
            (int)pending_ack->sequence_number,
            (int)decoded.header.sequence_number);
        return;
    }

    if (decoded.header.ack_status != AckStatus::OK) {
        notify_delivery_failure();
        return;
    }

    notify_logical_ack();
}

void TxManager::tx_task_func(void* arg)
{
    static_cast<TxManager*>(arg)->tx_task();

    // Should never reach here — tx_task() self-deletes via freertos_hal_
    vTaskDelete(NULL);
}

void TxManager::handle_esp_now_send_errors(esp_err_t error)
{
    if (error == ESP_ERR_ESPNOW_NO_MEM) {
        // Transient: do not penalize the FSM, ACK timeout will handle retry
        ESP_LOGW(TAG, "hal_esp_now_send: out of memory, will retry via timeout");
    }
    else if (error == ESP_ERR_ESPNOW_NOT_INIT || error == ESP_ERR_ESPNOW_ARG) {
        // Programming errors: log and discard
        ESP_LOGE(TAG, "hal_esp_now_send: unrecoverable error %s", esp_err_to_name(error));
    }
    else {
        // ESP_ERR_ESPNOW_NOT_FOUND, CHAN, IF, INTERNAL — link-level failures
        ESP_LOGW(TAG, "hal_esp_now_send failed: %s", esp_err_to_name(error));
        fsm_.on_delivery_failure();
    }
}

void TxManager::handle_notifications(uint32_t notifications, bool& should_stop)
{
    // Multiple notification bits can arrive simultaneously and must all be
    // processed. else-if would silently drop bits after the first match.
    if ((notifications & NOTIFY_LINK_ALIVE) == NOTIFY_LINK_ALIVE) {
        fsm_.on_link_alive();
    }
    // Each NOTIFY_DELIVERY_FAILURE is delegated to FSM decide if MAX_FAILURES was reached
    if ((notifications & NOTIFY_DELIVERY_FAILURE) == NOTIFY_DELIVERY_FAILURE) {
        // FSM check if MAX_FAILURES was reached and observer should be notified
        bool max_failures = fsm_.on_delivery_failure();
        if (max_failures) {
            // Notify RX task that max failures were reached
            ESP_LOGW(TAG, "Max failures reached, notifying RX task");
            freertos_hal_.task_notify(rx_task_handle_, NOTIFY_MAX_FAILURES, eSetBits);
        }
    }
    if ((notifications & NOTIFY_DELIVERY_SUCCESS) == NOTIFY_DELIVERY_SUCCESS) {
        fsm_.on_delivery_success();
    }
    if ((notifications & NOTIFY_LOGICAL_ACK) == NOTIFY_LOGICAL_ACK) {
        fsm_.on_ack_received();
        freertos_hal_.timer_stop(ack_timeout_timer_, pdMS_TO_TICKS(10));
    }
    if ((notifications & NOTIFY_ACK_TIMEOUT) == NOTIFY_ACK_TIMEOUT) {
        fsm_.on_ack_timeout();
    }
    if ((notifications & NOTIFY_TASK_TO_STOP) == NOTIFY_TASK_TO_STOP) {
        should_stop = true;
    }
}

void TxManager::tx_task()
{
    DecodedTxPacket structured_packet;
    TxPacket raw_packet;
    bool should_stop = false;
    uint32_t notifications = 0;

    ESP_LOGI(TAG, "TX Manager task started.");

    while (!should_stop) {
        TxState current_state = fsm_.get_state();

        // 1. Process pending notifications without blocking.
        // This ensures they are handled even if the queue is full and we are IDLE,
        // or if we are returning from a non-blocking state like RETRYING or SCANNING.
        if (freertos_hal_.task_notify_wait(0, NOTIFY_ALL, &notifications, 0) == pdTRUE) {
            handle_notifications(notifications, should_stop);
            if (should_stop) {
                break;
            }
            // Refresh state in case notifications triggered a transition
            current_state = fsm_.get_state();
        }

        switch (current_state) {
        case TxState::IDLE:
        {
            // Non-blocking queue read: the queue alone does not control sleep.
            if (freertos_hal_.queue_receive(tx_queue_, &structured_packet, 0) == pdTRUE) {
                // Update sequence number in header before encoding
                structured_packet.header.sequence_number = sequence_counter_++;

                // Encode the packet into raw wire format
                raw_packet.len = codec_.encode(
                    structured_packet.header,
                    structured_packet.payload,
                    structured_packet.payload_len,
                    raw_packet.data,
                    sizeof(raw_packet.data));

                if (raw_packet.len == 0) {
                    ESP_LOGE(TAG, "Failed to encode packet");
                    break;
                }

                memcpy(raw_packet.dest_mac, structured_packet.dest_mac, 6);
                raw_packet.requires_ack = structured_packet.header.requires_ack;

                esp_err_t send_result =
                    hal_espnow_.hal_esp_now_send(raw_packet.dest_mac, raw_packet.data, raw_packet.len);

                if (send_result == ESP_OK) {
                    TxState next = fsm_.on_packet_sent(raw_packet.requires_ack);
                    if (next == TxState::WAITING_FOR_ACK) {
                        PendingAck pending = {
                            .sequence_number = structured_packet.header.sequence_number,
                            .timestamp_ms = 0,
                            .retries_left = 3,
                            .packet = raw_packet,
                            .node_id = structured_packet.header.dest_node_id};
                        fsm_.set_pending_ack(pending);
                        freertos_hal_.timer_start(ack_timeout_timer_, pdMS_TO_TICKS(10));
                    }
                }
                else {
                    handle_esp_now_send_errors(send_result);
                }
            }
            else {
                // Queue is empty. The task blocks here waiting for any notification.
                // When queue_packet() is called, it sends NOTIFY_DATA to wake up this wait.
                if (freertos_hal_.task_notify_wait(0, 0xFFFFFFFF, &notifications, portMAX_DELAY) == pdTRUE) {
                    handle_notifications(notifications, should_stop);
                }
            }
            break;
        }

        case TxState::WAITING_FOR_ACK:
        {
            // The task MUST NOT consume new packets while waiting for a network ACK from the peer.
            // It blocks here completely until a notification arrives (like NOTIFY_LOGICAL_ACK, NOTIFY_ACK_TIMEOUT, or
            // NOTIFY_DELIVERY_FAILURE).
            if (freertos_hal_.task_notify_wait(0, 0xFFFFFFFF, &notifications, portMAX_DELAY) == pdTRUE) {
                // If a NOTIFY_LOGICAL_ACK arrives, handle_notifications() warns the TxStateMachine and stops the timer.
                // If a NOTIFY_ACK_TIMEOUT arrives, the FSM transitions to RETRYING.
                handle_notifications(notifications, should_stop);
                if (should_stop) {
                    break;
                }
            }
            break;
        }

        case TxState::RETRYING:
        {
            // Immediate packet resend logic. Does not block awaiting notifications.
            // Pending notifications are safely handled at the top of the next while loop iteration.
            auto pending_opt = fsm_.get_pending_ack();
            if (pending_opt && pending_opt->retries_left > 0) {
                PendingAck pending = pending_opt.value();
                pending.retries_left--;
                fsm_.set_pending_ack(pending);

                esp_err_t send_result =
                    hal_espnow_.hal_esp_now_send(pending.packet.dest_mac, pending.packet.data, pending.packet.len);

                if (send_result == ESP_OK) {
                    // Retry sent successfully, go back to WAITING_FOR_ACK and wait for the response again.
                    freertos_hal_.timer_start(ack_timeout_timer_, pdMS_TO_TICKS(10));
                    fsm_.on_packet_sent(true); // Back to WAITING_FOR_ACK
                }
                else {
                    handle_esp_now_send_errors(send_result);
                }
            }
            else {
                // Retries exhausted, notify FSM to drop the packet and potentially sever the link state.
                fsm_.on_max_retries();
            }
            break;
        }

        default:
            ESP_LOGE(TAG, "Unknown TxState: %d", static_cast<int>(current_state));
            fsm_.reset();
            break;
        }
    }

    ESP_LOGI(TAG, "TX Manager task exiting.");
    tx_task_handle_ = nullptr;
    freertos_hal_.semaphore_give(task_done_semaphore_);
    freertos_hal_.task_suspend(nullptr); // NULL / nullptr == current task
    freertos_hal_.task_delete(nullptr);  // NULL / nullptr == current task
}

// =====================================================================================
// Private methods
// =====================================================================================

void TxManager::notify_logical_ack()
{
    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_LOGICAL_ACK, eSetBits);
    }
}
