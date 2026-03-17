// mock_tx_state_machine.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_tx_state_machine.hpp"

class MockTxStateMachine : public ITxStateMachine
{
public:
    MOCK_METHOD(TxState, on_tx_success, (bool requires_ack), (override));
    MOCK_METHOD(TxState, on_ack_received, (), (override));
    MOCK_METHOD(TxState, on_ack_timeout, (), (override));
    MOCK_METHOD(TxState, on_physical_fail, (), (override));
    MOCK_METHOD(TxState, on_max_retries, (), (override));
    MOCK_METHOD(TxState, on_scan_requested, (), (override));
    MOCK_METHOD(void, on_link_alive, (), (override));
    MOCK_METHOD(TxState, get_state, (), (const, override));
    MOCK_METHOD(void, reset, (), (override));
    MOCK_METHOD(void, set_pending_ack, (const PendingAck &pending_ack), (override));
    MOCK_METHOD(std::optional<PendingAck>, get_pending_ack, (), (const, override));
};