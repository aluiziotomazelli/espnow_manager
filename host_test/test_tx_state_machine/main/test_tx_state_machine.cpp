#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "tx_state_machine.hpp"

class TxStateMachineTest : public ::testing::Test
{
protected:
    TxStateMachine fsm;

    // Helper to create a dummy PendingAck
    PendingAck make_pending_ack()
    {
        PendingAck ack = {};
        ack.sequence_number = 1;
        return ack;
    }
};

TEST_F(TxStateMachineTest, InitialStateIsIdle)
{
    EXPECT_EQ(TxState::IDLE, fsm.get_state());
}

TEST_F(TxStateMachineTest, OnAckTimeoutTransitionsToRetrying)
{
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());
}

TEST_F(TxStateMachineTest, ResetSetsStateToIdle)
{
    // On ack timeout sends to RETRYING
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());

    // reset must set state to IDLE
    fsm.reset();
    EXPECT_EQ(TxState::IDLE, fsm.get_state());
}

TEST_F(TxStateMachineTest, OnAckReceivedClearsPendingAck)
{
    fsm.set_pending_ack(make_pending_ack());
    EXPECT_TRUE(fsm.get_pending_ack().has_value());

    fsm.on_ack_received();
    EXPECT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_F(TxStateMachineTest, OnAckReceivedSetsStateToIdle)
{
    // On ack timeout sends to RETRYING
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());

    fsm.on_ack_received();
    EXPECT_EQ(TxState::IDLE, fsm.get_state());
}

TEST_F(TxStateMachineTest, OnMaxRetriesClearPendingAck)
{
    fsm.set_pending_ack(make_pending_ack());
    EXPECT_TRUE(fsm.get_pending_ack().has_value());

    fsm.on_max_retries();
    EXPECT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_F(TxStateMachineTest, OnMaxRetriesSetsStateToIdle)
{
    // On ack timeout sends to RETRYING
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());

    fsm.on_max_retries();
    EXPECT_EQ(TxState::IDLE, fsm.get_state());
}

TEST_F(TxStateMachineTest, ResetClearsPendingAck)
{
    fsm.set_pending_ack(make_pending_ack());
    EXPECT_TRUE(fsm.get_pending_ack().has_value());

    fsm.reset();
    EXPECT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_F(TxStateMachineTest, OnTxSuccessWithAckSetsStateToWaitingForAck)
{
    // On ack timeout sends to RETRYING
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());

    // on_tx_success(bool requires_ack = true) sends to WAITING_FOR_ACK
    fsm.on_tx_success(true);
    EXPECT_EQ(TxState::WAITING_FOR_ACK, fsm.get_state());
}

TEST_F(TxStateMachineTest, OnTxSuccessWithoutAckSetsStateToIdle)
{
    // On ack timeout sends to RETRYING
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());

    // on_tx_success(bool requires_ack = false) sends to IDLE
    fsm.on_tx_success(false);
    EXPECT_EQ(TxState::IDLE, fsm.get_state());
}

TEST_F(TxStateMachineTest, PhysicalFailSendToRetrying)
{
    fsm.on_link_alive();
    EXPECT_EQ(TxState::IDLE, fsm.get_state());

    fsm.on_physical_fail();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());
}

TEST_F(TxStateMachineTest, PhysicalFailBehavior)
{
    fsm.on_ack_timeout();
    EXPECT_EQ(TxState::RETRYING, fsm.get_state());

    // Fail MAX_FAILURES - 1 times
    for (int i = 0; i < MAX_FAILURES - 1; i++) {
        EXPECT_FALSE(fsm.on_physical_fail()) << "Should return false before MAX_FAILURES";
        EXPECT_EQ(TxState::RETRYING, fsm.get_state());
        EXPECT_EQ(fsm.get_fail_count(), i + 1);
    }
    // MAX_FAILURES-th failure should reset and return true
    EXPECT_TRUE(fsm.on_physical_fail()) << "Should return true at MAX_FAILURES";
    EXPECT_EQ(TxState::IDLE, fsm.get_state());
    EXPECT_EQ(fsm.get_fail_count(), 0); // Reset!
}

TEST_F(TxStateMachineTest, MaxFailuresClearsPendingAck)
{
    // Set a pending ack
    fsm.set_pending_ack(make_pending_ack());
    EXPECT_TRUE(fsm.get_pending_ack().has_value());

    for (int i = 0; i < MAX_FAILURES; i++) {
        fsm.on_physical_fail();
    }
    EXPECT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_F(TxStateMachineTest, OnLinkAliveClearFailCount)
{
    // Call on_physical_fail MAX_FAILURES - 1 times
    for (int i = 0; i < MAX_FAILURES - 1; i++) {
        fsm.on_physical_fail();
    }

    fsm.on_link_alive(); // Call on_link_alive to clear the fail count

    EXPECT_EQ(fsm.get_fail_count(), 0);
}
