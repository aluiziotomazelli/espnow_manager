#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "tx_manager.hpp"

static const char *TAG = "TxManager";

TxManager::TxManager(
    ITxStateMachine &fsm,
    IChannelScanner &scanner,
    IWiFiHAL &hal,
    IFreeRTOSHAL &freertos_hal,
    IMessageCodec &codec)
    : fsm_(fsm)
    , scanner_(scanner)
    , hal_(hal)
    , codec_(codec)
    , freertos_hal_(freertos_hal)
    , sequence_counter_(0)
    , task_done_semaphore_(nullptr)
    , tx_queue_(nullptr)
    , task_handle_(nullptr)
    , ack_timeout_timer_(nullptr)
{
}

TxManager::~TxManager()
{
    // deinit(); must be called before destruction to clean up resources
}

void TxManager::ack_timeout_callback(TimerHandle_t xTimer)
{
    TxManager *self = static_cast<TxManager *>(pvTimerGetTimerID(xTimer));
    self->freertos_hal_.task_notify(self->task_handle_, NOTIFY_ACK_TIMEOUT, eSetBits);
}

esp_err_t TxManager::init(uint32_t stack_size, UBaseType_t priority)
{
    tx_queue_ = freertos_hal_.queue_create(20, sizeof(TxPacket));
    if (!tx_queue_) {
        return ESP_ERR_NO_MEM;
    }

    task_done_semaphore_ = freertos_hal_.semaphore_create_binary();
    if (!task_done_semaphore_) {
        deinit();
        return ESP_ERR_NO_MEM;
    }

    ack_timeout_timer_ = freertos_hal_.timer_create("ack_timeout", 500, pdFALSE, this, ack_timeout_callback);
    if (!ack_timeout_timer_) {
        deinit();
        return ESP_ERR_NO_MEM;
    }

    BaseType_t task_creation =
        freertos_hal_.task_create(tx_task_func, "tx_manager_task", stack_size, this, priority, &task_handle_);
    if (task_creation != pdPASS) {
        deinit();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t TxManager::deinit()
{
    if (task_handle_) {
        // Notify task to stop
        freertos_hal_.task_notify(task_handle_, NOTIFY_STOP, eSetBits);

        // Send packet to weakup task
        TxPacket stop_packet = {};
        if (tx_queue_)
            freertos_hal_.queue_send(tx_queue_, &stop_packet, 0);

        // Wait for task to exit
        uint8_t delay = 10;
        for (int timeout = 1000; timeout > 0; timeout -= delay) {
            if (freertos_hal_.semaphore_take(task_done_semaphore_, delay) == pdPASS)
                break;
        }
        // Forcing deleting task
        if (task_handle_) {
            ESP_LOGW(TAG, "Forcing deletion of tx manager task");
            freertos_hal_.task_delete(task_handle_);
            task_handle_ = nullptr;
        }
    }

    if (task_done_semaphore_) {
        freertos_hal_.semaphore_delete(task_done_semaphore_);
        task_done_semaphore_ = nullptr;
    }

    if (tx_queue_) {
        freertos_hal_.queue_delete(tx_queue_);
        tx_queue_ = nullptr;
    }

    if (ack_timeout_timer_) {
        freertos_hal_.timer_delete(ack_timeout_timer_, PORT_MAX_DELAY);
        ack_timeout_timer_ = nullptr;
    }

    return ESP_OK;
}

esp_err_t TxManager::queue_packet(const TxPacket &packet)
{
    if (!tx_queue_)
        return ESP_ERR_INVALID_STATE;

    if (freertos_hal_.queue_send(tx_queue_, &packet, 100) != pdTRUE)
        return ESP_FAIL;

    if (task_handle_)
        freertos_hal_.task_notify(task_handle_, NOTIFY_DATA, eSetBits);

    return ESP_OK;
}

void TxManager::notify_physical_fail()
{
    if (task_handle_)
        freertos_hal_.task_notify(task_handle_, NOTIFY_PHYSICAL_FAIL, eSetBits);
}
void TxManager::notify_link_alive()
{
    if (task_handle_)
        freertos_hal_.task_notify(task_handle_, NOTIFY_LINK_ALIVE, eSetBits);
}
void TxManager::notify_logical_ack()
{
    if (task_handle_)
        freertos_hal_.task_notify(task_handle_, NOTIFY_LOGICAL_ACK, eSetBits);
}
void TxManager::notify_hub_found()
{
    if (task_handle_)
        freertos_hal_.task_notify(task_handle_, NOTIFY_HUB_FOUND, eSetBits);
}

void TxManager::tx_task_func(void *arg)
{
    static_cast<TxManager *>(arg)->run();

    // Should never reach here — run() self-deletes via freertos_hal_
    vTaskDelete(NULL);
}

void TxManager::run()
{
    TxPacket packet_to_send;
    ESP_LOGI(TAG, "TX Manager task started.");

    while (true) {
        uint32_t notifications = 0;
        TxState current_state = fsm_.get_state();

        switch (current_state) {
        case TxState::IDLE:
        {
            if (freertos_hal_.queue_receive(tx_queue_, &packet_to_send, 0) == pdTRUE) {
                // Update sequence number
                MessageHeader *header = reinterpret_cast<MessageHeader *>(packet_to_send.data);
                header->sequence_number = sequence_counter_++;
                // Update CRC
                packet_to_send.data[packet_to_send.len - CRC_SIZE] =
                    codec_.calculate_crc(packet_to_send.data, packet_to_send.len - CRC_SIZE);

                esp_err_t send_result =
                    hal_.hal_esp_now_send(packet_to_send.dest_mac, packet_to_send.data, packet_to_send.len);

                TxState next = fsm_.on_tx_success(packet_to_send.requires_ack && send_result == ESP_OK);
                if (next == TxState::WAITING_FOR_ACK) {
                    PendingAck pending = {
                        .sequence_number = header->sequence_number,
                        .timestamp_ms = 0,
                        .retries_left = 3,
                        .packet = packet_to_send,
                        .node_id = header->dest_node_id};
                    fsm_.set_pending_ack(pending);
                    freertos_hal_.timer_start(ack_timeout_timer_, 0);
                }
                break;
            }

            if (freertos_hal_.task_notify_wait(0, 0xFFFFFFFF, &notifications, PORT_MAX_DELAY) == pdTRUE) {
                if (notifications & NOTIFY_STOP)
                    goto exit;
                if (notifications & NOTIFY_LINK_ALIVE)
                    fsm_.on_link_alive();
                if (notifications & NOTIFY_PHYSICAL_FAIL)
                    fsm_.on_physical_fail();
            }
            break;
        }

        case TxState::WAITING_FOR_ACK:
        {
            if (freertos_hal_.task_notify_wait(0, 0xFFFFFFFF, &notifications, PORT_MAX_DELAY) == pdTRUE) {
                if (notifications & NOTIFY_STOP)
                    goto exit;
                if (notifications & NOTIFY_LINK_ALIVE)
                    fsm_.on_link_alive();
                if (notifications & NOTIFY_LOGICAL_ACK) {
                    fsm_.on_ack_received();
                    xTimerStop(ack_timeout_timer_, 0);
                }
                else if (notifications & NOTIFY_PHYSICAL_FAIL) {
                    fsm_.on_physical_fail();
                }
                else if (notifications & NOTIFY_ACK_TIMEOUT) {
                    fsm_.on_ack_timeout();
                }
            }
            break;
        }

        case TxState::RETRYING:
        {
            auto pending_opt = fsm_.get_pending_ack();
            if (pending_opt && pending_opt->retries_left > 0) {
                PendingAck pending = pending_opt.value();
                pending.retries_left--;
                fsm_.set_pending_ack(pending);

                esp_err_t send_result =
                    hal_.hal_esp_now_send(pending.packet.dest_mac, pending.packet.data, pending.packet.len);
                if (send_result != ESP_OK) {
                    fsm_.on_physical_fail();
                    break;
                }
                freertos_hal_.timer_start(ack_timeout_timer_, 0);
                fsm_.on_tx_success(true); // Back to WAITING_FOR_ACK
            }
            else {
                fsm_.on_max_retries();
            }
            break;
        }

        case TxState::SCANNING:
        {
            uint8_t current_channel = 1;
            hal_.wifi_get_channel(&current_channel);
            auto result = scanner_.scan(current_channel);
            if (result.hub_found) {
                hal_.wifi_set_channel(result.channel);
                fsm_.on_link_alive();
            }
            fsm_.reset(); // Back to IDLE
            break;
        }
        }
    }

exit:
    ESP_LOGI(TAG, "TX Manager task exiting.");
    task_handle_ = nullptr;
    freertos_hal_.semaphore_give(task_done_semaphore_);
    freertos_hal_.task_delete(nullptr);
}
