#pragma once

#include "i_tx_state_machine.hpp"

/**
 * @brief Concrete implementation of ITxStateMachine.
 *
 * Manages transmission state for individual packets with retry logic.
 * All methods are called from tx_task context only.
 *
 * @see ITxStateMachine for interface documentation.
 */
class TxStateMachine : public ITxStateMachine
{
public:
    TxStateMachine();
    ~TxStateMachine() override = default;

    /** @copydoc ITxStateMachine::on_packet_sent(bool) */
    TxState on_packet_sent(bool requires_ack) override;

    /** @copydoc ITxStateMachine::on_ack_received() */
    TxState on_ack_received() override;

    /** @copydoc ITxStateMachine::on_ack_timeout() */
    TxState on_ack_timeout() override;

    /** @copydoc ITxStateMachine::on_max_retries() */
    TxState on_max_retries() override;

    /** @copydoc ITxStateMachine::on_link_alive() */
    void on_link_alive() override;

    /** @copydoc ITxStateMachine::on_delivery_failure() */
    bool on_delivery_failure() override;

    /** @copydoc ITxStateMachine::on_delivery_success(bool) */
    void on_delivery_success() override;

    /** @copydoc ITxStateMachine::get_state() */
    TxState get_state() const override { return current_state_; }

    /** @copydoc ITxStateMachine::get_fail_count() */
    uint8_t get_fail_count() const override { return send_fail_count_; }

    /** @copydoc ITxStateMachine::reset() */
    void reset() override;

    /** @copydoc ITxStateMachine::set_pending_ack(const PendingAck&) */
    void set_pending_ack(const PendingAck& pending_ack) override;

    /** @copydoc ITxStateMachine::get_pending_ack() const */
    std::optional<PendingAck> get_pending_ack() const override { return pending_ack_; }

private:
    TxState current_state_;                 ///< Current transmission state
    std::optional<PendingAck> pending_ack_; ///< Pending ACK information
    uint8_t send_fail_count_;               ///< Consecutive failure counter
};
