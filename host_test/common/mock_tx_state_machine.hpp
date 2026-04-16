// mock_tx_state_machine.hpp
#pragma once

#include <gmock/gmock.h>
#include "i_tx_state_machine.hpp"

/**
 * @brief Mock implementation of ITxStateMachine for unit testing.
 *
 * Use this mock to verify state machine interactions in unit tests.
 * All methods are mocked using Google Mock.
 *
 * Example usage:
 * @code
 * MockTxStateMachine mock_fsm;
 * EXPECT_CALL(mock_fsm, on_delivery_failure())
 *     .WillOnce(Return(true));  // MAX_FAILURES reached
 * @endcode
 */
class MockTxStateMachine : public ITxStateMachine
{
public:
    /** @copydoc ITxStateMachine::on_packet_sent(bool) */
    MOCK_METHOD(TxState, on_packet_sent, (bool requires_ack), (override));

    /** @copydoc ITxStateMachine::on_ack_received() */
    MOCK_METHOD(TxState, on_ack_received, (), (override));

    /** @copydoc ITxStateMachine::on_ack_timeout() */
    MOCK_METHOD(TxState, on_ack_timeout, (), (override));

    /** @copydoc ITxStateMachine::on_max_retries() */
    MOCK_METHOD(TxState, on_max_retries, (), (override));

    /** @copydoc ITxStateMachine::on_link_alive() */
    MOCK_METHOD(void, on_link_alive, (), (override));

    /** @copydoc ITxStateMachine::on_delivery_failure() */
    MOCK_METHOD(bool, on_delivery_failure, (), (override));

    /** @copydoc ITxStateMachine::on_delivery_success() */
    MOCK_METHOD(void, on_delivery_success, (), (override));

    /** @copydoc ITxStateMachine::get_state() */
    MOCK_METHOD(TxState, get_state, (), (const, override));

    /** @copydoc ITxStateMachine::get_fail_count() */
    MOCK_METHOD(uint8_t, get_fail_count, (), (const, override));

    /** @copydoc ITxStateMachine::reset() */
    MOCK_METHOD(void, reset, (), (override));

    /** @copydoc ITxStateMachine::set_pending_ack(const PendingAck&) */
    MOCK_METHOD(void, set_pending_ack, (const PendingAck& pending_ack), (override));

    /** @copydoc ITxStateMachine::get_pending_ack() const */
    MOCK_METHOD(std::optional<PendingAck>, get_pending_ack, (), (const, override));
};