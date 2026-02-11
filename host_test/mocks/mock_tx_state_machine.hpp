#pragma once

#include "espnow_interfaces.hpp"
#include <optional>

class MockTxStateMachine : public ITxStateMachine
{
public:
    inline TxState on_tx_success(bool requires_ack) override
    {
        return TxState::IDLE;
    }
    inline TxState on_ack_received() override
    {
        return TxState::IDLE;
    }
    inline TxState on_ack_timeout() override
    {
        return TxState::RETRYING;
    }
    inline TxState on_physical_fail() override
    {
        return TxState::IDLE;
    }
    inline TxState on_max_retries() override
    {
        return TxState::IDLE;
    }
    inline void on_link_alive() override
    {
    }
    inline TxState get_state() const override
    {
        return TxState::IDLE;
    }
    inline void reset() override
    {
    }
    inline void set_pending_ack(const PendingAck &pending_ack) override
    {
    }
    inline std::optional<PendingAck> get_pending_ack() const override
    {
        return std::nullopt;
    }
};
