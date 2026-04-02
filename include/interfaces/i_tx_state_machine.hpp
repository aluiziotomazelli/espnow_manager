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
 * - IDLE → WAITING_FOR_ACK: When a packet requiring ACK is sent
 * - WAITING_FOR_ACK → RETRYING: On ACK timeout
 * - WAITING_FOR_ACK → IDLE: On ACK received
 * - RETRYING → IDLE: On successful retransmission or max retries reached
 *
 * State transitions are triggered by TxManager based on transmission results.
 * SCANNING state was removed - channel discovery is handled by NodeStateMachine
 * via ITxFailureObserver callback when MAX_FAILURES is reached.
 *
 * @note All methods are intended to be called from tx_task context only.
 * @note Thread safety: Not thread-safe - single task ownership assumed.
 * @internal
 */
class ITxStateMachine
{
public:
    virtual ~ITxStateMachine() = default;

    /**
     * @brief Handle successful packet transmission.
     * @param requires_ack If true, transition to WAITING_FOR_ACK; otherwise IDLE.
     * @return New state after transition.
     * @note Resets failure counter.
     */
    virtual TxState on_packet_sent(bool requires_ack) = 0;

    /**
     * @brief Handle logical ACK reception.
     * @return New state after transition (always IDLE).
     * @note Resets failure counter and clears pending ACK.
     */
    virtual TxState on_ack_received() = 0;

    /**
     * @brief Handle ACK timeout.
     * @return New state after transition (always RETRYING).
     * @note Does not increment failure counter - that's done on physical fail.
     */
    virtual TxState on_ack_timeout() = 0;

    /**
     * @brief Handle max retries reached - abort current packet.
     * @return New state after transition (always IDLE).
     * @note Clears pending ACK.
     */
    virtual TxState on_max_retries() = 0;

    /**
     * @brief Handle link alive notification.
     * @note Resets failure counter only - no state transition.
     * @note Called when any packet is received from the destination.
     */
    virtual void on_link_alive() = 0;

    /**
     * @brief Handle packet delivery failure.
     * @return true if MAX_FAILURES reached (observer should be notified).
     * @return false if still retrying.
     * @note Increments failure counter. Resets at MAX_FAILURES.
     * @internal
     */
    virtual bool on_delivery_failure() = 0;

    /**
     * @brief Get current transmission state.
     * @return Current TxState value.
     * @internal
     */
    virtual TxState get_state() const = 0;

    /**
     * @brief Get current failure count.
     * @return Number of consecutive failures (0 to MAX_FAILURES).
     * @internal
     */
    virtual uint8_t get_fail_count() const = 0;

    /**
     * @brief Reset state machine to initial state.
     * @note Clears pending ACK and resets failure counter.
     * @internal
     */
    virtual void reset() = 0;

    /**
     * @brief Set pending ACK information.
     * @param pending_ack Pending ACK details.
     * @internal
     */
    virtual void set_pending_ack(const PendingAck& pending_ack) = 0;

    /**
     * @brief Get pending ACK information.
     * @return Pending ACK details, or nullopt if none.
     * @internal
     */
    virtual std::optional<PendingAck> get_pending_ack() const = 0;
};