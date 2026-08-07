#include <cstring>

#include "esp_log.h"

#include "tx_manager.hpp"

namespace espnow {

static const char* TAG = "TxManager";

TxManager::TxManager(
    ITxStateMachine& fsm,
    IEspNowHAL& hal_espnow,
    IFreeRTOSHAL& freertos_hal,
    ITimerHAL& hal_timer,
    IMessageCodec& codec,
    IStatisticsManager& stats_mgr,
    IPeerManager& peer_mgr)
    : fsm_(fsm)
    , hal_espnow_(hal_espnow)
    , codec_(codec)
    , freertos_hal_(freertos_hal)
    , hal_timer_(hal_timer)
    , stats_mgr_(stats_mgr)
    , peer_mgr_(peer_mgr)
    , sequence_counter_(0)
    , ack_timeout_ms_(0)
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

esp_err_t
TxManager::init(uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle, uint32_t ack_timeout_ms)
{
    if (rx_task_handle == nullptr) {
        ESP_LOGE(TAG, "RX task handle is null");
        return ESP_ERR_INVALID_ARG;
    }
    rx_task_handle_ = rx_task_handle;
    ack_timeout_ms_ = ack_timeout_ms;

    tx_queue_ = freertos_hal_.queue_create(20, sizeof(DecodedTxPacket));
    if (tx_queue_ == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    delivery_queue_ = freertos_hal_.queue_create(2, sizeof(DeliveryEvent));
    if (delivery_queue_ == nullptr) {
        deinit();
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
        freertos_hal_.task_create(tx_task_func, "tx_task", stack_size, this, priority, &tx_task_handle_);
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

    if (delivery_queue_ != nullptr) {
        freertos_hal_.queue_delete(delivery_queue_);
        delivery_queue_ = nullptr;
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

    const bool requires_ack = packet.header.requires_ack;
    EventGroupHandle_t eg = nullptr;
    DecodedTxPacket pkt_copy = packet;

    if (requires_ack) {
        eg = freertos_hal_.event_group_create();
        if (eg == nullptr)
            return ESP_ERR_NO_MEM;
        pkt_copy.ack_event_group = eg;
    }

    if (freertos_hal_.queue_send(tx_queue_, &pkt_copy, 100) != pdTRUE) {
        if (eg != nullptr) {
            freertos_hal_.event_group_delete(eg);
        }
        return ESP_FAIL;
    }

    // Notify the TX task that a new packet is available in the queue
    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_DATA, eSetBits);
    }

    // If not require ack, returns imediately if message has queued successfully
    if (!requires_ack) {
        return ESP_OK;
    }

    // If requires ack, wait for the Ack, max retries reached or timeout (from ack or internal timer)
    const EventBits_t RESULT_BITS = NOTIFY_LOGICAL_ACK | NOTIFY_ACK_TIMEOUT | NOTIFY_MAX_FAILURES | NOTIFY_TASK_TO_STOP;
    const TickType_t wait_ticks = pdMS_TO_TICKS(ack_timeout_ms_ * (MAX_FAILURES + 1) + 100);

    EventBits_t bits = freertos_hal_.event_group_wait_bits(
        eg,
        RESULT_BITS,
        pdFALSE, // don't clear bits — the group is deleted shortly
        pdFALSE, // any bit (not all)
        wait_ticks);

    freertos_hal_.event_group_delete(eg); // Delete the event group after waiting

    // If task stopped,
    if ((bits & NOTIFY_TASK_TO_STOP) == NOTIFY_TASK_TO_STOP) {
        return ESP_ERR_INVALID_STATE;
    }
    if ((bits & NOTIFY_LOGICAL_ACK) == NOTIFY_LOGICAL_ACK) {
        return ESP_OK;
    }
    if ((bits & NOTIFY_ACK_TIMEOUT) == NOTIFY_ACK_TIMEOUT) {
        return ESP_ERR_TIMEOUT;
    }
    if ((bits & NOTIFY_MAX_FAILURES) == NOTIFY_MAX_FAILURES) {
        return ESP_FAIL;
    }
    return ESP_ERR_TIMEOUT; // Timeout waiting for ACK or max failures
}

void TxManager::notify_delivery(esp_now_send_status_t status, const uint8_t* dest_mac)
{
    if (tx_task_handle_ == nullptr || delivery_queue_ == nullptr || dest_mac == nullptr) {
        return;
    }
    DeliveryEvent event;
    memcpy(event.dest_mac, dest_mac, 6);
    event.status = static_cast<uint8_t>(status);
    BaseType_t higher_prio_woken = pdFALSE;
    freertos_hal_.queue_send_fromISR(delivery_queue_, &event, &higher_prio_woken);

    // Set ONLY the relevant notification bit
    uint32_t notify_bit = (status == ESP_NOW_SEND_FAIL) ? NOTIFY_DELIVERY_FAILURE : NOTIFY_DELIVERY_SUCCESS;
    freertos_hal_.task_notify(tx_task_handle_, notify_bit, eSetBits);
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
        ESP_LOGW(
            TAG,
            "Command rejected by node 0x%02X (status: %d). Aborting retries.",
            static_cast<uint8_t>(pending_ack->node_id),
            static_cast<int>(decoded.header.ack_status));
        stats_mgr_.on_delivery_failure(pending_ack->node_id);
        notify_logical_ack(); // Stop retrying a rejected command
        return;
    }

    // Calculate RTT: current packet timestamp (us) - pending packet timestamp (us)
    uint32_t rtt_us = static_cast<uint32_t>(decoded.raw.timestamp_us - pending_ack->timestamp_us);
    stats_mgr_.on_ack_received(pending_ack->node_id, rtt_us);

    notify_logical_ack();
}

void TxManager::tx_task_func(void* arg)
{
    static_cast<TxManager*>(arg)->tx_task();

    // Should never reach here — tx_task() self-deletes via freertos_hal_
    vTaskDelete(NULL);
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
                // Captures the event group from the packet (nullptr if non-blocking)
                caller_ack_event_group_ = structured_packet.ack_event_group;

                // Update sequence number only for non-ACK packets.
                // ACKs must preserve the sequence number of the packet they are acknowledging.
                if (structured_packet.header.msg_type != MessageType::ACK) {
                    structured_packet.header.sequence_number = sequence_counter_++;
                }

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
                            .timestamp_us = hal_timer_.get_time_us(),
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
                    stats_mgr_.on_retry(pending.node_id);
                }
                else {
                    handle_esp_now_send_errors(send_result);
                }
            }
            else {
                // Retries exhausted, notify FSM to drop the packet and potentially sever the link state.
                auto pending_opt = fsm_.get_pending_ack();
                if (pending_opt) {
                    stats_mgr_.on_packet_lost(pending_opt->node_id);
                }
                fsm_.on_max_retries();

                // Notify queue_packet() that peers has reacheable but no logical ACK was received, if applicable
                if (caller_ack_event_group_ != nullptr) {
                    freertos_hal_.event_group_set_bits(caller_ack_event_group_, NOTIFY_ACK_TIMEOUT);
                    caller_ack_event_group_ = nullptr;
                }
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
    freertos_hal_.task_delete(nullptr); // NULL / nullptr == current task
}

// =====================================================================================
// Private methods
// =====================================================================================

void TxManager::handle_notifications(uint32_t notifications, bool& should_stop)
{
    // Multiple notification bits can arrive simultaneously and must all be
    // processed. else-if would silently drop bits after the first match.
    if ((notifications & NOTIFY_LINK_ALIVE) == NOTIFY_LINK_ALIVE) {
        fsm_.on_link_alive();
    }

    if ((notifications & NOTIFY_DELIVERY_FAILURE) == NOTIFY_DELIVERY_FAILURE) {
        DeliveryEvent event{};
        while (freertos_hal_.queue_receive(delivery_queue_, &event, 0) == pdTRUE) {
            NodeId node_id = 0;
            esp_err_t err = peer_mgr_.find_node_id_by_mac(event.dest_mac, node_id);
            if (err == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Delivery event: mutex timeout resolving MAC");
                continue;
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Delivery event for unregistered MAC — unexpected after protocol fix");
                continue;
            }
            stats_mgr_.on_delivery_failure(node_id);
        }
        // FSM check if MAX_FAILURES was reached and observer should be notified
        bool max_failures = fsm_.on_delivery_failure();
        if (max_failures) {
            ESP_LOGW(TAG, "Max failures reached, notifying RX task");
            freertos_hal_.task_notify(rx_task_handle_, NOTIFY_MAX_FAILURES, eSetBits);

            // Notify queue_packet() that max failures were reached, if applicable
            if (caller_ack_event_group_ != nullptr) {
                freertos_hal_.event_group_set_bits(caller_ack_event_group_, NOTIFY_MAX_FAILURES);
                caller_ack_event_group_ = nullptr;
            }
        }
    }
    if ((notifications & NOTIFY_DELIVERY_SUCCESS) == NOTIFY_DELIVERY_SUCCESS) {
        DeliveryEvent event{};
        while (freertos_hal_.queue_receive(delivery_queue_, &event, 0) == pdTRUE) {
            NodeId node_id = 0;
            esp_err_t err = peer_mgr_.find_node_id_by_mac(event.dest_mac, node_id);
            if (err == ESP_ERR_TIMEOUT) {
                ESP_LOGW(TAG, "Delivery event: mutex timeout resolving MAC");
                continue;
            }
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Delivery event for unregistered MAC — unexpected after protocol fix");
                continue;
            }
            stats_mgr_.on_delivery_success(node_id);
        }
        fsm_.on_delivery_success();
    }
    if ((notifications & NOTIFY_LOGICAL_ACK) == NOTIFY_LOGICAL_ACK) {
        fsm_.on_ack_received();
        freertos_hal_.timer_stop(ack_timeout_timer_, pdMS_TO_TICKS(10));

        // Signal the waiting queue_packet() call that the ACK was received, if applicable
        if (caller_ack_event_group_ != nullptr) {
            freertos_hal_.event_group_set_bits(caller_ack_event_group_, NOTIFY_LOGICAL_ACK);
            caller_ack_event_group_ = nullptr;
        }
    }
    if ((notifications & NOTIFY_ACK_TIMEOUT) == NOTIFY_ACK_TIMEOUT) {
        fsm_.on_ack_timeout();
    }
    if ((notifications & NOTIFY_TASK_TO_STOP) == NOTIFY_TASK_TO_STOP) {
        should_stop = true;
        if (caller_ack_event_group_ != nullptr) {
            freertos_hal_.event_group_set_bits(caller_ack_event_group_, NOTIFY_TASK_TO_STOP);
            caller_ack_event_group_ = nullptr;
        }
    }
}

void TxManager::handle_esp_now_send_errors(esp_err_t error)
{
    auto pending_opt = fsm_.get_pending_ack();
    NodeId node_id = pending_opt ? pending_opt->node_id : 0;

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
        stats_mgr_.on_driver_error(node_id);
        ESP_LOGW(TAG, "hal_esp_now_send failed: %s", esp_err_to_name(error));
        fsm_.on_delivery_failure();
    }
}

void TxManager::notify_logical_ack()
{
    if (tx_task_handle_ != nullptr) {
        freertos_hal_.task_notify(tx_task_handle_, NOTIFY_LOGICAL_ACK, eSetBits);
    }
}

} // namespace espnow
