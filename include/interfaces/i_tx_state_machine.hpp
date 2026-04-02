// include/interfaces/i_tx_state_machine.hpp
#pragma once

#include <optional>

#include "espnow_types.hpp"
#include "protocol_messages.hpp"

/**
 * @brief Transmission states for individual packets.
 *
 * Note: SCANNING was removed - channel discovery is now handled
 * by NodeStateMachine via ITxFailureObserver callback.
 */
enum class TxState
{
    IDLE,            ///< No active transmission
    WAITING_FOR_ACK, ///< Physical send success, waiting for logical ACK
    RETRYING,        ///< Waiting before retransmission attempt
    COUNT            ///< Number of states (for validation)
};

/**
 * @interface ITxStateMachine
 * @brief State machine for managing transmission lifecycle and retries.
 *
 * This interface defines the state transitions for individual packet transmission:
 *
 * **State Transition Table:**
 *
 * | Current State     | Event/Method               | Next State        | Description                    |
 * |-------------------|----------------------------|-------------------|--------------------------------|
 * | IDLE              | on_packet_sent(true)       | WAITING_FOR_ACK   | Packet sent, awaiting ACK      |
 * | IDLE              | on_packet_sent(false)      | IDLE              | Fire-and-forget packet sent    |
 * | IDLE              | on_delivery_success(true)  | WAITING_FOR_ACK   | Physical send success          |
 * | IDLE              | on_delivery_success(false) | IDLE              | Physical send success (no ACK) |
 * | WAITING_FOR_ACK   | on_ack_received()          | IDLE              | Logical ACK received           |
 * | WAITING_FOR_ACK   | on_ack_timeout()           | RETRYING          | ACK timeout, prepare retry     |
 * | WAITING_FOR_ACK   | on_delivery_failure()      | RETRYING          | Physical send failed           |
 * | RETRYING          | on_packet_sent()           | WAITING_FOR_ACK   | Retry attempt                  |
 * | RETRYING          | on_max_retries()           | IDLE              | Max retries exhausted          |
 * | ANY               | reset()                    | IDLE              | Force reset to initial state   |
 *
 * @note SCANNING state was removed - channel discovery is handled by NodeStateMachine
 *       via ITxFailureObserver callback when MAX_FAILURES is reached.
 * @note All methods are intended to be called from tx_task context only.
 * @note Thread safety: Not thread-safe - single task ownership assumed.
 * @internal
 */
class ITxStateMachine
{
public:
    virtual ~ITxStateMachine() = default;

    /**
     * @brief Handle successful packet transmission (physical layer).
     *
     * Called when a packet has been successfully handed to the ESP-NOW driver.
     * Transitions to WAITING_FOR_ACK if acknowledgment is required, otherwise
     * remains in IDLE (fire-and-forget transmission).
     *
     * @param requires_ack True if the packet requires a protocol-level logical ACK.
     * @return TxState::WAITING_FOR_ACK: If requires_ack is true.
     * @return TxState::IDLE: If requires_ack is false.
     * @note Resets the failure counter.
     * @note This handles physical layer success only. Protocol-level acknowledgment
     *       is handled separately by on_ack_received().
     * @see on_delivery_success()
     * @see on_ack_received()
     */
    virtual TxState on_packet_sent(bool requires_ack) = 0;

    /**
     * @brief Handle ESP-NOW send callback success (ESP_NOW_SEND_SUCCESS).
     *
     * Called from the ESP-NOW transmission callback when the packet is successfully
     * delivered to the peer at the physical layer. This is distinct from receiving
     * a protocol-level logical ACK.
     *
     * @note This indicates physical layer success only. The peer may still not
     *       process the packet correctly - protocol-level ACK provides that guarantee.
     * @see on_packet_sent()
     * @see on_ack_received()
     * @see on_delivery_failure()
     */
    virtual void on_delivery_success() = 0;

    /**
     * @brief Handle protocol-level acknowledgment (ACK) reception.
     *
     * Called when an ACK packet is received from the destination peer, confirming
     * that the transmitted packet was successfully received and processed at the
     * application layer.
     *
     * This completes the reliable transmission cycle:
     * 1. Packet sent (on_packet_sent or on_delivery_success)
     * 2. WAITING_FOR_ACK state
     * 3. ACK received (this method) → IDLE
     *
     * @return TxState::IDLE: Always transitions to IDLE.
     * @note Resets the failure counter.
     * @note Clears the pending ACK state.
     * @note This is distinct from physical layer success (on_delivery_success).
     * @see on_delivery_success()
     * @see on_ack_timeout()
     */
    virtual TxState on_ack_received() = 0;

    /**
     * @brief Handle logical ACK timeout.
     *
     * Called when the ACK timeout timer expires while in WAITING_FOR_ACK state.
     * The packet will be retransmitted (up to MAX_FAILURES times) before the
     * link is considered lost.
     *
     * @return TxState::RETRYING: Always transitions to RETRYING.
     * @note Does not increment the failure counter - incremented on physical
     *       transmission failure in on_delivery_failure().
     * @note After transitioning to RETRYING, the TxManager will schedule a
     *       retransmission after the backoff delay.
     * @see on_delivery_failure()
     * @see on_max_retries()
     */
    virtual TxState on_ack_timeout() = 0;

    /**
     * @brief Abort current packet after exhausting all retry attempts.
     *
     * Called when MAX_FAILURES consecutive transmission failures have occurred
     * and no further retries should be attempted. The pending packet is discarded
     * and the state machine returns to IDLE.
     *
     * After this method, the caller (TxManager) should:
     * 1. Discard the current packet from the queue
     * 2. Notify ITxFailureObserver to initiate channel scanning
     * 3. Continue processing remaining queued packets
     *
     * @return TxState::IDLE: Always transitions to IDLE.
     * @note Clears the pending ACK state.
     * @note The failure counter is reset to 0.
     * @see on_delivery_failure()
     * @see ITxFailureObserver
     */
    virtual TxState on_max_retries() = 0;

    /**
     * @brief Handle link alive notification.
     *
     * Called when any valid packet is received from the destination peer,
     * indicating that the communication link is active. This prevents unnecessary
     * channel scanning by resetting the failure counter.
     *
     * @note No state transition occurs - only the failure counter is reset.
     * @note Called from the RX path after CRC validation passes.
     * @note The peer identification is handled internally via the current
     *       transmission context.
     * @see HeartbeatManager
     */
    virtual void on_link_alive() = 0;

    /**
     * @brief Handle ESP-NOW send callback failure (ESP_NOW_SEND_FAIL).
     *
     * Called from the ESP-NOW transmission callback when the packet fails to
     * transmit at the physical layer. This increments the failure counter and
     * determines whether to retry or notify the observer for channel scanning.
     *
     * Failure handling flow:
     * - If fail_count < MAX_FAILURES: Increment counter, return true (trigger retry)
     * - If fail_count >= MAX_FAILURES: Reset counter, return true (trigger scan)
     *
     * @return true: MAX_FAILURES reached - observer should initiate channel scanning.
     * @return false: Still retrying - TxManager will schedule retransmission.
     * @note Increments the failure counter.
     * @note Counter is reset when a packet is successfully delivered or ACK received.
     * @see on_delivery_success()
     * @see on_max_retries()
     * @see ITxFailureObserver
     */
    virtual bool on_delivery_failure() = 0;

    /**
     * @brief Get current transmission state.
     *
     * Returns the current state of the transmission state machine.
     * Primarily used for debugging, logging, and state assertions.
     *
     * @return TxState::IDLE: No active transmission.
     * @return TxState::WAITING_FOR_ACK: Awaiting logical ACK.
     * @return TxState::RETRYING: Preparing for retransmission.
     * @note This is a const method - safe to call from any context.
     */
    virtual TxState get_state() const = 0;

    /**
     * @brief Get current consecutive failure count.
     *
     * Returns the number of consecutive transmission failures for the
     * current packet or link. This counter is used to determine when
     * to trigger channel scanning (at MAX_FAILURES).
     *
     * @return uint8_t: Number of failures (range: 0 to MAX_FAILURES).
     * @note Counter is reset on successful transmission or ACK reception.
     * @note When this reaches MAX_FAILURES, ITxFailureObserver is notified.
     * @see on_delivery_failure()
     * @see ITxFailureObserver
     */
    virtual uint8_t get_fail_count() const = 0;

    /**
     * @brief Reset state machine to initial state (IDLE).
     *
     * Forces the state machine back to its initial state, clearing all pending
     * operations. This is typically called during:
     * - Manager initialization
     * - Connection re-establishment
     * - Error recovery scenarios
     *
     * @note Transitions to TxState::IDLE regardless of current state.
     * @note Clears the pending ACK state.
     * @note Resets the failure counter to 0.
     * @note Any queued packet awaiting acknowledgment is discarded.
     */
    virtual void reset() = 0;

    /**
     * @brief Set pending ACK information for the current transmission.
     *
     * Stores the expected ACK details (sequence number, destination, timeout)
     * for the packet currently being transmitted. This is used to validate
     * incoming ACKs and detect timeouts.
     *
     * @param pending_ack Pending ACK details including sequence number and timeout.
     * @note Called after a packet is sent that requires acknowledgment.
     * @note The pending ACK is cleared when on_ack_received() or on_max_retries()
     *       is called.
     * @see get_pending_ack()
     * @see on_ack_received()
     */
    virtual void set_pending_ack(const PendingAck& pending_ack) = 0;

    /**
     * @brief Get pending ACK information for the current transmission.
     *
     * Retrieves the stored ACK details for validating incoming acknowledgments
     * or checking timeout status.
     *
     * @return PendingAck: Details of the pending acknowledgment.
     * @return std::nullopt: No pending ACK (state is IDLE or fire-and-forget packet).
     * @see set_pending_ack()
     * @see on_ack_received()
     */
    virtual std::optional<PendingAck> get_pending_ack() const = 0;
};