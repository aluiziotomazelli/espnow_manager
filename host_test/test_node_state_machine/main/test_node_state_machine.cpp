#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "node_state_machine.hpp"
#include "espnow_types.hpp"

class NodeStateMachineTest : public ::testing::Test
{
protected:
    NodeStateMachine fsm;
};

// ===================================
// 1. Initialization
// ===================================

// When initialized with peers (from storage), it should go straight to OPERATIONAL.
TEST_F(NodeStateMachineTest, InitWithPeersTransitionsToOperational)
{
    EXPECT_EQ(fsm.on_init(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// When initialized without peers, it should start searching for a HUB (PAIRING_SCAN).
TEST_F(NodeStateMachineTest, InitWithoutPeersTransitionsToPairingScan)
{
    EXPECT_EQ(fsm.on_init(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// ===================================
// 2. Pairing Requests
// ===================================

// From IDLE (no peers), pairing request requires a channel scan.
TEST_F(NodeStateMachineTest, PairingRequestedFromIdleWithoutPeersTransitionsToPairingScan)
{
    fsm.on_init(false);
    fsm.on_scan_failed(false); // Go to IDLE
    ASSERT_EQ(fsm.get_state(), NodeState::IDLE);

    EXPECT_EQ(fsm.on_pairing_requested(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// From IDLE (but has peers, e.g. after a recovery scan failed), pairing request can start directly.
TEST_F(NodeStateMachineTest, PairingRequestedFromIdleWithPeersTransitionsToPairing)
{
    fsm.on_init(true);
    fsm.on_scan_requested();
    fsm.on_scan_failed(true); // Go to IDLE (per revised table)
    ASSERT_EQ(fsm.get_state(), NodeState::IDLE);

    EXPECT_EQ(fsm.on_pairing_requested(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// From OPERATIONAL (has peers), pairing request starts directly.
TEST_F(NodeStateMachineTest, PairingRequestedFromOperationalWithPeersTransitionsToPairing)
{
    fsm.on_init(true);
    ASSERT_EQ(fsm.get_state(), NodeState::OPERATIONAL);

    EXPECT_EQ(fsm.on_pairing_requested(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// Edge case: if in OPERATIONAL but all peers removed, pairing request needs scan.
TEST_F(NodeStateMachineTest, PairingRequestedFromOperationalWithoutPeersTransitionsToPairingScan)
{
    fsm.on_init(true);
    ASSERT_EQ(fsm.get_state(), NodeState::OPERATIONAL);

    EXPECT_EQ(fsm.on_pairing_requested(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// = ==================================
// 3. Channel Scanning
// ===================================

// When link is lost in OPERATIONAL, it should start RECOVERY_SCAN.
TEST_F(NodeStateMachineTest, ScanRequestedFromOperationalTransitionsToRecoveryScan)
{
    fsm.on_init(true);
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::RECOVERY_SCAN);
}

// If channel found during PAIRING_SCAN, move to PAIRING.
TEST_F(NodeStateMachineTest, ChannelFoundInPairingScanTransitionsToPairing)
{
    fsm.on_init(false); // PAIRING_SCAN
    EXPECT_EQ(fsm.on_channel_found(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// If channel found during RECOVERY_SCAN, move back to OPERATIONAL.
TEST_F(NodeStateMachineTest, ChannelFoundInRecoveryScanTransitionsToOperational)
{
    fsm.on_init(true);
    fsm.on_scan_requested(); // RECOVERY_SCAN
    EXPECT_EQ(fsm.on_channel_found(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// = ==================================
// 4. Scan Failures
// = ==================================

// If scan for pairing fails, go to IDLE.
TEST_F(NodeStateMachineTest, ScanFailedInPairingScanTransitionsToIdle)
{
    fsm.on_init(false); // PAIRING_SCAN
    EXPECT_EQ(fsm.on_scan_failed(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// If scan for recovery fails, go to IDLE.
TEST_F(NodeStateMachineTest, ScanFailedInRecoveryScanTransitionsToIdle)
{
    fsm.on_init(true);
    fsm.on_scan_requested(); // RECOVERY_SCAN
    EXPECT_EQ(fsm.on_scan_failed(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// = ==================================
// 5. Pairing Completion
// = ==================================

// Successful pairing (has_peers = true) leads to OPERATIONAL.
TEST_F(NodeStateMachineTest, PairingSucceededTransitionsToOperational)
{
    fsm.on_init(false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_pairing_timeout(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// Failed pairing without existing peers leads to IDLE.
TEST_F(NodeStateMachineTest, PairingFailedWithoutPeersTransitionsToIdle)
{
    fsm.on_init(false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_pairing_timeout(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// Failed pairing WITH existing peers (e.g. adding a second node failed) returns to OPERATIONAL.
TEST_F(NodeStateMachineTest, PairingFailedWithPeersTransitionsToOperational)
{
    fsm.on_init(true);
    fsm.on_pairing_requested(true); // PAIRING
    EXPECT_EQ(fsm.on_pairing_timeout(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// = ==================================
// 6. Lifecycle
// ==================================

TEST_F(NodeStateMachineTest, DeinitTransitionsToUninitialized)
{
    fsm.on_init(true);
    EXPECT_EQ(fsm.on_deinit(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

TEST_F(NodeStateMachineTest, ResetTransitionsToUninitialized)
{
    fsm.on_init(true);
    fsm.reset();
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}