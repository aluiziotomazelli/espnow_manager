#pragma once

#include "espnow_interfaces.hpp"

class RealTxStateMachine : public ITxStateMachine
{
public:
    RealTxStateMachine();

    TxState on_tx_success(bool requires_ack) override;
    TxState on_ack_received() override;
    TxState on_ack_timeout() override;
    TxState on_physical_fail() override;
    TxState on_max_retries() override;
    void on_link_alive() override;

    TxState get_state() const override { return current_state_; }
    void reset() override;

    void set_pending_ack(const PendingAck &pending_ack) override;
    std::optional<PendingAck> get_pending_ack() const override { return pending_ack_; }

private:
    TxState current_state_;
    std::optional<PendingAck> pending_ack_;
    uint8_t phy_send_fail_count_;
    uint8_t phy_consecutive_fail_count_;
};
