#pragma once

#include "espnow_interfaces.hpp"
#include <optional>

class MockTxStateMachine : public ITxStateMachine
{
public:
    TxState on_tx_success_ret   = TxState::IDLE;
    TxState on_ack_received_ret = TxState::IDLE;
    TxState on_ack_timeout_ret  = TxState::RETRYING;
    TxState on_physical_fail_ret = TxState::IDLE;
    TxState on_max_retries_ret   = TxState::IDLE;

    int on_tx_success_calls   = 0;
    int on_ack_received_calls = 0;
    int on_ack_timeout_calls  = 0;
    int on_physical_fail_calls = 0;
    int on_max_retries_calls   = 0;
    int on_link_alive_calls    = 0;
    int reset_calls            = 0;

    bool last_requires_ack = false;
    std::optional<PendingAck> last_pending_ack;

    inline TxState on_tx_success(bool requires_ack) override
    {
        on_tx_success_calls++;
        last_requires_ack = requires_ack;
        return on_tx_success_ret;
    }

    inline TxState on_ack_received() override
    {
        on_ack_received_calls++;
        return on_ack_received_ret;
    }

    inline TxState on_ack_timeout() override
    {
        on_ack_timeout_calls++;
        return on_ack_timeout_ret;
    }

    inline TxState on_physical_fail() override
    {
        on_physical_fail_calls++;
        return on_physical_fail_ret;
    }

    inline TxState on_max_retries() override
    {
        on_max_retries_calls++;
        return on_max_retries_ret;
    }

    inline void on_link_alive() override
    {
        on_link_alive_calls++;
    }

    inline TxState get_state() const override
    {
        return current_state_ret;
    }

    inline void reset() override
    {
        reset_calls++;
    }

    inline void set_pending_ack(const PendingAck &pending_ack) override
    {
        last_pending_ack = pending_ack;
    }

    inline std::optional<PendingAck> get_pending_ack() const override
    {
        return last_pending_ack;
    }

    void reset_mock()
    {
        on_tx_success_calls    = 0;
        on_ack_received_calls  = 0;
        on_ack_timeout_calls   = 0;
        on_physical_fail_calls = 0;
        on_max_retries_calls   = 0;
        on_link_alive_calls    = 0;
        reset_calls             = 0;

        on_tx_success_ret   = TxState::IDLE;
        on_ack_received_ret = TxState::IDLE;
        on_ack_timeout_ret  = TxState::RETRYING;
        on_physical_fail_ret = TxState::IDLE;
        on_max_retries_ret   = TxState::IDLE;
        current_state_ret    = TxState::IDLE;

        last_pending_ack.reset();
    }

    TxState current_state_ret = TxState::IDLE;
};
