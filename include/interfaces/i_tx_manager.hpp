// include/interfaces/i_tx_manager.hpp
#pragma once

#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "espnow_types.hpp"

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
 * @internal
 */
class ITxManager
{
public:
    virtual ~ITxManager() = default;

    /**
     * @brief Initialize the TxManager and start the background task.
     * @param stack_size Stack size for the background TX task.
     * @param priority Priority for the background TX task.
     * @param rx_task_handle Handle of the RX task (used for synchronization/notifications).
     * @return ESP_OK on success, or an error code.
     * @internal
     */
    virtual esp_err_t init(uint32_t stack_size, UBaseType_t priority, TaskHandle_t rx_task_handle) = 0;

    /**
     * @brief Stop the background task and clean up resources.
     * @internal
     */
    virtual void deinit() = 0;

    /**
     * @brief Add a packet to the transmission queue.
     * @param packet The decoded packet to be sent.
     * @return ESP_OK if queued successfully, ESP_ERR_NO_MEM if queue is full.
     */
    virtual esp_err_t queue_packet(const DecodedTxPacket& packet) = 0;

    /**
     * @brief Notify the manager about a physical layer transmission failure.
     *
     * This is typically called from the ESP-NOW send callback when status is FAIL.
     * It triggers retransmission or failure handling in the state machine.
     * @internal
     */
    virtual void notify_physical_fail() = 0;

    /**
     * @brief Notify the manager that a peer is still alive/reachable.
     *
     * Called when any packet is received from a peer, indicating the link is active.
     * Resets failure counters for that peer.
     * @internal
     */
    virtual void notify_link_alive() = 0;

    /**
     * @brief Notify the manager that a logical ACK has been received.
     *
     * Called by the RX path when a protocol-level ACK arrives for a pending packet.
     * @internal
     */
    virtual void notify_logical_ack() = 0;

    /**
     * @brief Get the background task handle.
     * @return TaskHandle_t of the TX task.
     * @internal
     */
    virtual TaskHandle_t get_task_handle() const = 0;
};
