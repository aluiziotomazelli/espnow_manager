#include "tx_state_machine.hpp"
#include "protocol_types.hpp"

RealTxStateMachine::RealTxStateMachine()
    : current_state_(TxState::IDLE)
    , pending_ack_(std::nullopt)
    , phy_send_fail_count_(0)
    , phy_consecutive_fail_count_(0)
{
}

void RealTxStateMachine::reset()
{
    current_state_ = TxState::IDLE;
    pending_ack_.reset();
    phy_send_fail_count_        = 0;
    phy_consecutive_fail_count_ = 0;
}

void RealTxStateMachine::set_pending_ack(const PendingAck &pending_ack)
{
    pending_ack_ = pending_ack;
}

TxState RealTxStateMachine::on_tx_success(bool requires_ack)
{
    if (requires_ack) {
        current_state_ = TxState::WAITING_FOR_ACK;
    }
    else {
        current_state_ = TxState::IDLE;
    }
    return current_state_;
}

TxState RealTxStateMachine::on_ack_received()
{
    phy_send_fail_count_        = 0;
    phy_consecutive_fail_count_ = 0;
    pending_ack_.reset();
    current_state_ = TxState::IDLE;
    return current_state_;
}

void RealTxStateMachine::on_link_alive()
{
    phy_consecutive_fail_count_ = 0;
    phy_send_fail_count_        = 0;
}

TxState RealTxStateMachine::on_ack_timeout()
{
    current_state_ = TxState::RETRYING;
    return current_state_;
}

TxState RealTxStateMachine::on_physical_fail()
{
    phy_consecutive_fail_count_++;

    if (pending_ack_) {
        phy_send_fail_count_++;
        if (phy_send_fail_count_ >= MAX_LOGICAL_RETRIES || phy_consecutive_fail_count_ >= MAX_PHYSICAL_FAILURES) {
            phy_send_fail_count_        = 0;
            phy_consecutive_fail_count_ = 0;
            pending_ack_.reset();
            current_state_ = TxState::SCANNING;
        }
        else {
            current_state_ = TxState::WAITING_FOR_ACK;
        }
    }
    else {
        if (phy_consecutive_fail_count_ >= MAX_PHYSICAL_FAILURES) {
            phy_consecutive_fail_count_ = 0;
            phy_send_fail_count_        = 0;
            current_state_              = TxState::SCANNING;
        }
        else {
            // For non-ACK packets, we just stay in IDLE or where we were?
            // Original code says: current_state remains IDLE or WAITING_FOR_ACK
        }
    }
    return current_state_;
}

TxState RealTxStateMachine::on_max_retries()
{
    pending_ack_.reset();
    current_state_ = TxState::IDLE;
    return current_state_;
}
