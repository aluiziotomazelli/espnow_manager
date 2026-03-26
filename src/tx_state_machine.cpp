#include "tx_state_machine.hpp"
#include "protocol_types.hpp"

TxStateMachine::TxStateMachine()
    : current_state_(TxState::IDLE)
    , pending_ack_(std::nullopt)
    , send_fail_count_(0)
{
}

TxStateMachine::~TxStateMachine() = default;

void TxStateMachine::reset()
{
    current_state_ = TxState::IDLE;
    pending_ack_.reset();
    send_fail_count_ = 0;
}

void TxStateMachine::set_pending_ack(const PendingAck& pending_ack)
{
    pending_ack_ = pending_ack;
}

TxState TxStateMachine::on_tx_success(bool requires_ack)
{
    send_fail_count_ = 0;

    if (requires_ack) {
        current_state_ = TxState::WAITING_FOR_ACK;
    }
    else {
        // Fire-and-forget packets: no ACK needed, return to IDLE immediately
        current_state_ = TxState::IDLE;
    }
    return current_state_;
}

TxState TxStateMachine::on_ack_received()
{
    send_fail_count_ = 0;
    pending_ack_.reset();
    current_state_ = TxState::IDLE;
    return current_state_;
}

void TxStateMachine::on_link_alive()
{
    send_fail_count_ = 0;
}

TxState TxStateMachine::on_ack_timeout()
{
    current_state_ = TxState::RETRYING;
    return current_state_;
}

bool TxStateMachine::on_physical_fail()
{
    send_fail_count_++;

    if (send_fail_count_ >= MAX_FAILURES) {
        // Reset for next packet
        send_fail_count_ = 0;
        if (pending_ack_) {
            pending_ack_.reset();
        }
        current_state_ = TxState::IDLE;
        return true;
    }

    current_state_ = TxState::RETRYING;
    return false;
}

TxState TxStateMachine::on_max_retries()
{
    pending_ack_.reset();
    current_state_ = TxState::IDLE;
    return current_state_;
}

uint8_t TxStateMachine::get_fail_count() const
{
    return send_fail_count_;
}
