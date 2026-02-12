#include "tx_state_machine.hpp"
#include "protocol_types.hpp"
#include "unity.h"
#include <optional>

TEST_CASE("TxStateMachine initial state is IDLE", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_tx_success transitions correctly", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // Requires ACK
    TxState state = fsm.on_tx_success(true);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, state);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.get_state());

    // Does not require ACK
    fsm.reset();
    state = fsm.on_tx_success(false);
    TEST_ASSERT_EQUAL(TxState::IDLE, state);
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
}

TEST_CASE("TxStateMachine on_ack_received returns to IDLE and resets", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = { .sequence_number = 1 };
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    TxState state = fsm.on_ack_received();
    TEST_ASSERT_EQUAL(TxState::IDLE, state);
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_ack_timeout transitions to RETRYING", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    fsm.on_tx_success(true);

    TxState state = fsm.on_ack_timeout();
    TEST_ASSERT_EQUAL(TxState::RETRYING, state);
}

TEST_CASE("TxStateMachine on_physical_fail with pending ack transitions to SCANNING after MAX_LOGICAL_RETRIES", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = { .sequence_number = 1 };
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    for (int i = 0; i < MAX_LOGICAL_RETRIES - 1; ++i)
    {
        TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.on_physical_fail());
    }
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail());

    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_physical_fail without pending ack transitions to SCANNING after 3 consecutive fails", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // No pending ack, just physical fails (maybe broadcast or something went wrong)
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.on_physical_fail()); // 1st fail
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.on_physical_fail()); // 2nd fail
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail()); // 3rd fail -> SCANNING
}

TEST_CASE("TxStateMachine on_link_alive resets consecutive fail counters", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    fsm.on_physical_fail(); // 1
    fsm.on_physical_fail(); // 2

    fsm.on_link_alive();    // Reset!

    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.on_physical_fail()); // 1 again (would be 3 and SCANNING if not reset)
}

TEST_CASE("TxStateMachine on_ack_received resets consecutive fail counters", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = { .sequence_number = 1 };
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    fsm.on_physical_fail(); // 1
    fsm.on_physical_fail(); // 2

    fsm.on_ack_received(); // Reset!

    fsm.on_physical_fail(); // 1 again
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
}

TEST_CASE("TxStateMachine on_max_retries returns to IDLE", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = { .sequence_number = 1 };
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    TxState state = fsm.on_max_retries();
    TEST_ASSERT_EQUAL(TxState::IDLE, state);
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine reset clears everything", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = { .sequence_number = 1 };
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);
    fsm.on_physical_fail(); // Increment counters

    fsm.reset();

    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());

    // Verify counters were reset by checking how many fails it takes to SCANNING
    fsm.on_physical_fail(); // 1
    fsm.on_physical_fail(); // 2
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail()); // 3
}
