#include "protocol_types.hpp"
#include "tx_state_machine.hpp"
#include "unity.h"
#include <optional>

/**
 * @file test_tx_state_machine.cpp
 * @brief Unit tests for the transmission state machine (FSM).
 *
 * The FSM manages states like IDLE, WAITING_FOR_ACK, RETRYING, and SCANNING
 * based on the success or failure of radio operations.
 */

TEST_CASE("TxStateMachine initial state is IDLE", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    // The state machine must always start in IDLE with no pending packets.
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_tx_success transitions correctly", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // Case 1: Packet requires a logical ACK.
    // FSM should move to WAITING_FOR_ACK to prevent sending new data until confirmed.
    TxState state = fsm.on_tx_success(true);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, state);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.get_state());

    // Case 2: Packet does NOT require ACK (e.g. Broadcast or Scan Response).
    // FSM should return to IDLE immediately.
    fsm.reset();
    state = fsm.on_tx_success(false);
    TEST_ASSERT_EQUAL(TxState::IDLE, state);
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
}

TEST_CASE("TxStateMachine on_ack_received returns to IDLE and resets", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = {.sequence_number = 1};
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.get_state());

    // Receiving the expected ACK should clear the pending packet and free the state machine.
    TxState state = fsm.on_ack_received();
    TEST_ASSERT_EQUAL(TxState::IDLE, state);
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_ack_timeout transitions to RETRYING", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    fsm.on_tx_success(true);
    TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.get_state());

    // If the logical timeout expires, we should enter RETRYING state.
    TxState state = fsm.on_ack_timeout();
    TEST_ASSERT_EQUAL(TxState::RETRYING, state);
}

TEST_CASE("TxStateMachine on_physical_fail with pending ack transitions to SCANNING after MAX_LOGICAL_RETRIES",
          "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = {.sequence_number = 1};
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    // If we are waiting for an ACK and the physical layer (ESP-NOW) fails repeatedly,
    // we should eventually give up and start SCANNING for the Hub.
    for (int i = 0; i < MAX_LOGICAL_RETRIES - 1; ++i) {
        TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.on_physical_fail());
        TEST_ASSERT_TRUE(fsm.get_pending_ack().has_value()); // Data still preserved for potential retry
    }

    // Final failure trigger transition to SCANNING
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail());

    // Pending data is cleared when entering SCANNING as the link is considered dead.
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_physical_fail without pending ack transitions to SCANNING after MAX_PHYSICAL_FAILURES",
          "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // No pending ack (e.g. failures sending broadcasts).
    // We still track consecutive physical failures to detect a dead link.
    for (int i = 0; i < MAX_PHYSICAL_FAILURES - 1; ++i) {
        TEST_ASSERT_EQUAL(TxState::IDLE, fsm.on_physical_fail());
    }

    // Beyond the limit, we assume the Hub is gone or on another channel.
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail());
}

TEST_CASE("TxStateMachine failures accumulate across non-ack successes", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // Verification that non-ACK packets (requires_ack=false) do NOT reset the failure counters.
    // This ensures that even if we can send "fire and forget" packets, we still detect
    // if most of our packets are actually failing at the driver level.
    for (int i = 0; i < MAX_PHYSICAL_FAILURES - 1; ++i) {
        fsm.on_physical_fail();
        fsm.on_tx_success(false);
    }

    // This failure should push us into SCANNING.
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail());
}

TEST_CASE("TxStateMachine mixed states triggering SCANNING via consecutive limit", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // Simulate a mix of physical failures and successful sends (that eventually lead to waiting for ACK).
    for (int i = 0; i < MAX_PHYSICAL_FAILURES - 1; ++i) {
        fsm.on_physical_fail();
        if (i == 0) {
            TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
        }
        PendingAck ack = {.sequence_number = 1};
        fsm.set_pending_ack(ack);
        fsm.on_tx_success(true);
        TEST_ASSERT_EQUAL(TxState::WAITING_FOR_ACK, fsm.get_state());
    }

    // Final failure triggers SCANNING.
    TxState state = fsm.on_physical_fail();

    TEST_ASSERT_EQUAL(TxState::SCANNING, state);
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine on_link_alive resets consecutive fail counters", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    // Generate some failures.
    fsm.on_physical_fail(); // 1
    fsm.on_physical_fail(); // 2

    // Any indication of a healthy link (e.g. received packet or successful ping) resets counters.
    fsm.on_link_alive();

    // Now it takes the full limit again to trigger SCANNING.
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.on_physical_fail());
}

TEST_CASE("TxStateMachine on_ack_received resets consecutive fail counters", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = {.sequence_number = 1};
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    fsm.on_physical_fail(); // 1
    fsm.on_physical_fail(); // 2

    // Receiving a logical ACK is the ultimate proof of a healthy bidirectional link.
    fsm.on_ack_received();

    fsm.on_physical_fail(); // Fail counter should have been reset to 1
    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
}

TEST_CASE("TxStateMachine set_pending_ack stores value correctly", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = {.sequence_number = 42, .node_id = 123};

    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());

    // Storage for retry logic.
    fsm.set_pending_ack(ack);

    auto stored = fsm.get_pending_ack();
    TEST_ASSERT_TRUE(stored.has_value());
    TEST_ASSERT_EQUAL(42, stored->sequence_number);
    TEST_ASSERT_EQUAL(123, stored->node_id);
}

TEST_CASE("TxStateMachine set_pending_ack overwrites existing value", "[tx_fsm]")
{
    RealTxStateMachine fsm;

    fsm.set_pending_ack(PendingAck{.sequence_number = 1, .node_id = 100});

    // If a new packet is sent before the previous is ACKed (unlikely in current design),
    // it should simply overwrite the previous pending state.
    fsm.set_pending_ack(PendingAck{.sequence_number = 2, .node_id = 200});

    auto stored = fsm.get_pending_ack();
    TEST_ASSERT_EQUAL(2, stored->sequence_number);
    TEST_ASSERT_EQUAL(200, stored->node_id);
}

TEST_CASE("TxStateMachine on_max_retries returns to IDLE", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = {.sequence_number = 1};
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);

    // If we exceed logical retries without a physical link fail (e.g. Hub exists but is dropping our data),
    // we return to IDLE and drop the packet.
    TxState state = fsm.on_max_retries();
    TEST_ASSERT_EQUAL(TxState::IDLE, state);
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());
}

TEST_CASE("TxStateMachine reset clears everything", "[tx_fsm]")
{
    RealTxStateMachine fsm;
    PendingAck ack = {.sequence_number = 1};
    fsm.set_pending_ack(ack);
    fsm.on_tx_success(true);
    fsm.on_physical_fail(); // Increment internal counters

    // Total factory reset of the FSM state.
    fsm.reset();

    TEST_ASSERT_EQUAL(TxState::IDLE, fsm.get_state());
    TEST_ASSERT_FALSE(fsm.get_pending_ack().has_value());

    // Verify internal counters were also reset by checking transition limits.
    for (int i = 0; i < MAX_PHYSICAL_FAILURES - 1; ++i) {
        TEST_ASSERT_EQUAL(TxState::IDLE, fsm.on_physical_fail());
    }
    TEST_ASSERT_EQUAL(TxState::SCANNING, fsm.on_physical_fail());
}
