// include/internface/i_tx_state_machine.hpp
#pragma once

#include <optional>

#include "espnow_types.hpp"
#include "protocol_messages.hpp"

/**
 * @interface ITxStateMachine
 * @brief State machine for managing transmission lifecycle and retries
 * (internal)
 * @internal
 */
class ITxStateMachine
{
public:
    virtual ~ITxStateMachine() = default;

    /** @internal */
    virtual TxState on_tx_success(bool requires_ack) = 0;
    /** @internal */
    virtual TxState on_ack_received() = 0;
    /** @internal */
    virtual TxState on_ack_timeout() = 0;
    /** @internal */
    virtual TxState on_physical_fail() = 0;
    /** @internal */
    virtual TxState on_max_retries() = 0;

    /** @internal */
    virtual TxState on_scan_requested() = 0;

    /** @internal */
    virtual void on_link_alive() = 0;

    /** @internal */
    virtual TxState get_state() const = 0;
    /** @internal */
    virtual void reset() = 0;
    /** @internal */
    virtual void set_pending_ack(const PendingAck &pending_ack) = 0;
    /** @internal */
    virtual std::optional<PendingAck> get_pending_ack() const = 0;
};