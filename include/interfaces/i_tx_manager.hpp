// include/interfaces/i_tx_manager.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "espnow_types.hpp"
namespace espnow {

/**
 * @brief Delivery event data passed from ISR callback to TX task.
 */
struct DeliveryEvent
{
    uint8_t dest_mac[6];
    uint8_t status; // esp_now_send_status_t
};

/**
 * @interface ITxManager
 * @brief Manager for transmission queue and background sending task.
 *
 * The TxManager is responsible for:
 * - Maintaining a thread-safe queue of packets to be sent.
 * - Managing a background task that processes the queue.
 * - Handling retransmissions and backoff logic (via TxStateMachine).
 * - Notifying observers about transmission failures.
 *
 * @note This is an internal component used by EspNowManager.
 * @note All public methods are thread-safe and may be called from any context.
 *       Callback notification methods (notify_*) may be called from ISR context.
 * @internal
 */
class ITxManager
{
public:
    virtual ~ITxManager() = default;

    /**
     * @brief Initializes the TxManager and starts the background task.
     * @param stack_size Stack size for the background TX task (in words).
     * @param priority Priority for the background TX task.
     * @param rx_task_handle Handle of the RX task (used for synchronization/notifications).
     * @param ack_timeout_ms Timeout for logical acknowledgments in milliseconds.
     * @return ESP_OK: Initialization successful, background task started.
     * @return ESP_ERR_NO_MEM: Failed to allocate task stack or queue memory.
     * @return ESP_FAIL: Failed to create background task.
     * @return ESP_ERR_INVALID_STATE: Already initialized.
     * @internal
     */
    virtual esp_err_t init(uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle, uint32_t ack_timeout_ms) = 0;

    /**
     * @brief Stops the background task and cleans up all allocated resources.
     *
     * This method:
     * - Stops the TX background task
     * - Deletes the task handle
     * - Clears the transmission queue
     * - Releases any allocated memory
     *
     * @note After calling deinit(), the manager must be re-initialized via init()
     *       before further use.
     * @note This method blocks until the background task has fully terminated.
     * @internal
     */
    virtual void deinit() = 0;

    /**
     * @brief Adds a packet to the transmission queue.
     *
     * The packet is copied into an internal queue and will be processed by the
     * background TX task. Queue processing follows FIFO order with priority
     * handling for retransmissions.
     *
     * @param packet The decoded packet to be sent.
     * @return ESP_OK: Packet queued successfully.
     * @return ESP_ERR_NO_MEM: Queue is full, packet dropped.
     * @return ESP_ERR_INVALID_STATE: Manager not initialized.
     *
     * @note This method is thread-safe and may be called from any task context.
     * @note The packet is copied internally; caller retains ownership of the input.
     */
    virtual esp_err_t queue_packet(const DecodedTxPacket& packet) = 0;

    /**
     * @brief Notifies the manager about a physical layer delivery result.
     *
     * This method is called from the ESP-NOW send callback (esp_now_send_cb_t) with
     * the transmission status (ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL) and the
     * destination MAC address. It routes the event to the TX task via a queue and
     * notification for processing.
     *
     * @param status The ESP-NOW send status (ESP_NOW_SEND_SUCCESS or ESP_NOW_SEND_FAIL).
     * @param dest_mac The 6-byte destination MAC address from the callback.
     *
     * @note This method may be called from interrupt context (ESP-NOW callback).
     *       Implementation must be ISR-safe.
     * @internal
     */
    virtual void notify_delivery(esp_now_send_status_t status, const uint8_t* dest_mac) = 0;

    /**
     * @brief Notifies the manager that a peer is still alive and reachable.
     *
     * This method is called from the RX path when any valid packet is received from
     * a peer. It indicates that the communication link is active and resets the
     * failure counters for that peer, preventing unnecessary channel scanning or
     * peer eviction.
     *
     * @note The peer identification is handled internally (typically via the currently
     *       processing context or packet metadata).
     * @note This method integrates with the HeartbeatManager to update link health
     *       monitoring.
     * @note Called after CRC validation passes, before message routing.
     * @see HeartbeatManager
     * @internal
     */
    virtual void notify_link_alive() = 0;

    /**
     * @brief Handles a received ACK packet with sequence number validation.
     *
     * This method is called by the RX path when an ACK packet arrives. It extracts
     * the sequence number from the ACK payload and validates it against the pending
     * transmission. If valid, it notifies the TX task to proceed.
     *
     * @param decoded The decoded ACK packet containing the sequence number.
     * @internal
     */
    virtual void handle_ack(const DecodedRxPacket& decoded) = 0;

    /**
     * @brief Gets the background task handle.
     *
     * Returns the FreeRTOS task handle for the TX background task. This can be used
     * for task monitoring, stack watermark checking, or task notifications.
     *
     * @return TaskHandle_t: Handle of the TX background task.
     * @return nullptr: If the manager is not initialized.
     *
     * @note The returned handle is valid only after successful init() and before
     *       deinit() is called.
     * @note Use uxTaskGetStackHighWaterMark() to monitor task stack usage.
     * @internal
     */
    virtual TaskHandle_t get_task_handle() const = 0;
};

} // namespace espnow
