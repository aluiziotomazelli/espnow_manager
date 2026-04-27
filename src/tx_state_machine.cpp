// src/tx_state_machine.cpp

#include "tx_state_machine.hpp"
#include "protocol_types.hpp"
#include "esp_log.h"

namespace espnow {

static const char* TAG = "TxStateMachine";

TxStateMachine::TxStateMachine()
    : current_state_(TxState::IDLE)
    , pending_ack_(std::nullopt)
    , send_fail_count_(0)
{
}

void TxStateMachine::reset()
{
    ESP_LOGV(TAG, "Resetting state machine");
    current_state_ = TxState::IDLE;
    pending_ack_.reset();
    send_fail_count_ = 0;
}

void TxStateMachine::set_pending_ack(const PendingAck& pending_ack)
{
    pending_ack_ = pending_ack;
}

TxState TxStateMachine::on_packet_sent(bool requires_ack)
{
    ESP_LOGV(TAG, "Packet sent notification");

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
    ESP_LOGV(TAG, "Ack received notification");
    send_fail_count_ = 0;
    pending_ack_.reset();
    current_state_ = TxState::IDLE;
    return current_state_;
}

void TxStateMachine::on_link_alive()
{
    ESP_LOGV(TAG, "Link alive notification");
    send_fail_count_ = 0;
}

TxState TxStateMachine::on_ack_timeout()
{
    current_state_ = TxState::RETRYING;
    return current_state_;
}

bool TxStateMachine::on_delivery_failure()
{
    ESP_LOGV(TAG, "Delivery failure notification, fail count: %d", send_fail_count_);

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

void TxStateMachine::on_delivery_success()
{
    send_fail_count_ = 0;
}

TxState TxStateMachine::on_max_retries()
{
    pending_ack_.reset();

    current_state_ = TxState::IDLE;
    return current_state_;
}

} // namespace espnow
