#include "tx_manager.hpp"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "TxManager";

// Notification Bits (aligned with espnow_manager.hpp for now)
static constexpr uint32_t NOTIFY_LOGICAL_ACK     = 0x01;
static constexpr uint32_t NOTIFY_PHYSICAL_FAIL   = 0x02;
static constexpr uint32_t NOTIFY_HUB_FOUND       = 0x04;
static constexpr uint32_t NOTIFY_DATA            = 0x20;
static constexpr uint32_t NOTIFY_ACK_TIMEOUT     = 0x40;
static constexpr uint32_t NOTIFY_STOP            = 0x100;
static constexpr uint32_t NOTIFY_LINK_ALIVE      = 0x200;

RealTxManager::RealTxManager(ITxStateMachine &fsm,
                             IChannelScanner &scanner,
                             IWiFiHAL &hal,
                             IMessageCodec &codec)
    : fsm_(fsm)
    , scanner_(scanner)
    , hal_(hal)
    , codec_(codec)
{
}

RealTxManager::~RealTxManager()
{
    deinit();
}

esp_err_t RealTxManager::init(uint32_t stack_size, UBaseType_t priority)
{
    tx_queue_ = xQueueCreate(20, sizeof(TxPacket));
    if (!tx_queue_) return ESP_ERR_NO_MEM;

    ack_timeout_timer_ = xTimerCreate("ack_timeout", pdMS_TO_TICKS(500), pdFALSE, this, [](TimerHandle_t xTimer) {
        RealTxManager *self = static_cast<RealTxManager *>(pvTimerGetTimerID(xTimer));
        if (self->task_handle_) {
            xTaskNotify(self->task_handle_, NOTIFY_ACK_TIMEOUT, eSetBits);
        }
    });

    if (xTaskCreate(tx_task_func, "tx_manager_task", stack_size, this, priority, &task_handle_) != pdPASS) {
        return ESP_FAIL;
    }

    hal_.set_task_to_notify(task_handle_);

    return ESP_OK;
}

esp_err_t RealTxManager::deinit()
{
    if (task_handle_) {
        xTaskNotify(task_handle_, NOTIFY_STOP, eSetBits);
        // Wait for task to exit
        vTaskDelay(pdMS_TO_TICKS(100));
        task_handle_ = nullptr;
    }

    if (tx_queue_) {
        vQueueDelete(tx_queue_);
        tx_queue_ = nullptr;
    }

    if (ack_timeout_timer_) {
        xTimerDelete(ack_timeout_timer_, portMAX_DELAY);
        ack_timeout_timer_ = nullptr;
    }

    return ESP_OK;
}

esp_err_t RealTxManager::queue_packet(const TxPacket &packet)
{
    if (!tx_queue_) return ESP_ERR_INVALID_STATE;
    if (xQueueSend(tx_queue_, &packet, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (task_handle_) {
        xTaskNotify(task_handle_, NOTIFY_DATA, eSetBits);
    }
    return ESP_OK;
}

void RealTxManager::notify_physical_fail() { if (task_handle_) xTaskNotify(task_handle_, NOTIFY_PHYSICAL_FAIL, eSetBits); }
void RealTxManager::notify_link_alive() { if (task_handle_) xTaskNotify(task_handle_, NOTIFY_LINK_ALIVE, eSetBits); }
void RealTxManager::notify_logical_ack() { if (task_handle_) xTaskNotify(task_handle_, NOTIFY_LOGICAL_ACK, eSetBits); }
void RealTxManager::notify_hub_found() { if (task_handle_) xTaskNotify(task_handle_, NOTIFY_HUB_FOUND, eSetBits); }

void RealTxManager::tx_task_func(void *arg)
{
    static_cast<RealTxManager *>(arg)->run();
    vTaskDelete(NULL);
}

void RealTxManager::run()
{
    TxPacket packet_to_send;
    ESP_LOGI(TAG, "TX Manager task started.");

    while (true) {
        uint32_t notifications = 0;
        TxState current_state = fsm_.get_state();

        switch (current_state) {
        case TxState::IDLE:
        {
            if (xQueueReceive(tx_queue_, &packet_to_send, 0) == pdTRUE) {
                // Transition to SENDING
                // We'll handle sending in the next loop iteration or just fall through
                // For simplicity, let's just use the logic from original task.

                MessageHeader *header = reinterpret_cast<MessageHeader *>(packet_to_send.data);
                header->sequence_number = sequence_counter_++;
                // Update CRC
                packet_to_send.data[packet_to_send.len - CRC_SIZE] = codec_.calculate_crc(packet_to_send.data, packet_to_send.len - CRC_SIZE);

                esp_err_t send_result = hal_.send_packet(packet_to_send.dest_mac, packet_to_send.data, packet_to_send.len);

                TxState next = fsm_.on_tx_success(packet_to_send.requires_ack && send_result == ESP_OK);
                if (next == TxState::WAITING_FOR_ACK) {
                    PendingAck pending = {
                        .sequence_number = header->sequence_number,
                        .timestamp_ms = 0,
                        .retries_left = 3,
                        .packet = packet_to_send,
                        .node_id = header->dest_node_id
                    };
                    fsm_.set_pending_ack(pending);
                    xTimerStart(ack_timeout_timer_, 0);
                }
                break;
            }

            if (xTaskNotifyWait(0, 0xFFFFFFFF, &notifications, portMAX_DELAY) == pdTRUE) {
                if (notifications & NOTIFY_STOP) goto exit;
                if (notifications & NOTIFY_LINK_ALIVE) fsm_.on_link_alive();
                if (notifications & NOTIFY_PHYSICAL_FAIL) {
                    if (fsm_.on_physical_fail() == TxState::SCANNING) {
                        // Handle scanning below
                    }
                }
                // NOTIFY_DATA is handled by xQueueReceive at start of loop
            }
            break;
        }

        case TxState::WAITING_FOR_ACK:
        {
            if (xTaskNotifyWait(0, 0xFFFFFFFF, &notifications, portMAX_DELAY) == pdTRUE) {
                if (notifications & NOTIFY_STOP) goto exit;
                if (notifications & NOTIFY_LINK_ALIVE) fsm_.on_link_alive();
                if (notifications & NOTIFY_LOGICAL_ACK) {
                    fsm_.on_ack_received();
                    xTimerStop(ack_timeout_timer_, 0);
                } else if (notifications & NOTIFY_PHYSICAL_FAIL) {
                    fsm_.on_physical_fail();
                } else if (notifications & NOTIFY_ACK_TIMEOUT) {
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

                hal_.send_packet(pending.packet.dest_mac, pending.packet.data, pending.packet.len);
                xTimerStart(ack_timeout_timer_, 0);
                fsm_.on_tx_success(true); // Back to WAITING_FOR_ACK
            } else {
                fsm_.on_max_retries();
            }
            break;
        }

        case TxState::SENDING:
            // This state is transient in our implementation
            break;

        case TxState::SCANNING:
        {
            uint8_t current_channel = 1;
            hal_.get_channel(&current_channel);
            auto result = scanner_.scan(current_channel);
            if (result.hub_found) {
                hal_.set_channel(result.channel);
                fsm_.on_link_alive();
            }
            fsm_.reset(); // Back to IDLE
            break;
        }
        }
    }

exit:
    ESP_LOGI(TAG, "TX Manager task exiting.");
}
