#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "node_state_machine.hpp"
#include "espnow_types.hpp"

class NodeStateMachineTest : public ::testing::Test
{
protected:
    NodeStateMachine fsm;
};

// =========================================================================
// Expected Behaviors (TDD)
// =========================================================================

// ========================================================================
// Init
// ========================================================================

TEST_F(NodeStateMachineTest, InitialStateIsUninitialized)
{
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

// Init without peers means it will start pairing to look for nodes to pair with
TEST_F(NodeStateMachineTest, InitSuccessWithoutPeersTransitionsToPairing)
{
    EXPECT_EQ(fsm.on_init(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// Init with peers means it already has peers and will start operational state
TEST_F(NodeStateMachineTest, InitSuccessWithPeersTransitionsToOperational)
{
    EXPECT_EQ(fsm.on_init(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

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

// ========================================================================
// Pairing
// ========================================================================

// If a node is in IDLE (no peers) state and pairing is requested, it will transition to PAIRING state
// that means it will start looking for a HUB to pair with
TEST_F(NodeStateMachineTest, PairingRequestedFromIdleTransitionsToPairing)
{
    // Reach IDLE first
    fsm.on_init(false);              // PAIRING
    fsm.on_pairing_completed(false); // IDLE
    ASSERT_EQ(fsm.get_state(), NodeState::IDLE);

    EXPECT_EQ(fsm.on_pairing_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// If a node is in OPERATIONAL (has peers) state and pairing is requested, it will transition to PAIRING state
// to pairing to others nodes
TEST_F(NodeStateMachineTest, PairingRequestedFromOperationalTransitionsToPairing)
{
    fsm.on_init(true);
    EXPECT_EQ(fsm.on_pairing_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// If a node in PAIRING state completes pairing with peers, it will transition to OPERATIONAL state
TEST_F(NodeStateMachineTest, PairingCompletedWithPeersTransitionsToOperational)
{
    fsm.on_init(false); // already in PAIRING
    EXPECT_EQ(fsm.on_pairing_completed(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// If a node in PAIRING state completes pairing without peers, it will transition to IDLE state
// that means it will stop looking for a HUB to pair with
TEST_F(NodeStateMachineTest, PairingCompletedWithoutPeersTransitionsToIdle)
{
    fsm.on_init(false); // already in PAIRING
    EXPECT_EQ(fsm.on_pairing_completed(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// ========================================================================
// Scan
// ========================================================================

// The case when a node in OPERATIONAL state requests a scan is because it already has peers
// and is trying to find HUB channel to reestablish connection
TEST_F(NodeStateMachineTest, ScanRequestedFromOperationalTransitionsToScanning)
{
    fsm.on_init(true);
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING);
}

// If a node is in PAIRING state and scan is requested, is because it wants to find a channel to pair
// so it will transition to SCANNING state
TEST_F(NodeStateMachineTest, ScanRequestedFromPairingTransitionsToScanning)
{
    fsm.on_init(false); // PAIRING
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING);
}

// ========================================================================
// Channel Found
// ========================================================================

// Usually HUB dont make scan, but this supports future "Bridge" types
// or mobile HUBs that might need to rediscover a channel in future implementations
TEST_F(NodeStateMachineTest, ChannelFoundByHubTransitionsToOperational)
{
    fsm.on_init(true);
    fsm.on_scan_requested();
    // HUB always returns to OPERATIONAL
    EXPECT_EQ(fsm.on_channel_found(true, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// Usually called by a node that has peers, but lost the channel from HUB
// and is trying to find it again, in case of success it returns to OPERATIONAL
TEST_F(NodeStateMachineTest, ChannelFoundByPeripheralInScanningWithPeersTransitionsToOperational)
{
    fsm.on_init(true);       // OPERATIONAL
    fsm.on_scan_requested(); // SCANNING
    // on_channel_found(bool is_hub, bool has_peers)
    EXPECT_EQ(fsm.on_channel_found(false, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// If a node has no peers and calls scan, is because it wants to pair
// and if it finds a channel, it will transition to PAIRING
TEST_F(NodeStateMachineTest, ChannelFoundByPeripheralWithoutPeersTransitionsToPairing)
{
    fsm.on_init(false); // PAIRING
    fsm.on_scan_requested();
    EXPECT_EQ(fsm.on_channel_found(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// ========================================================================
// Scan Failed
// ========================================================================

// If a node in in PAIRING state and scan fails, but pairing timeout was not
// reached, it will retry scan, going to SCANNING to try again
TEST_F(NodeStateMachineTest, ScanFailedInPairingWhilePairingIsStillActiveTransitionsToScanning)
{
    fsm.on_init(false); // PAIRING
    // on_scan_failed(bool is_pairing_active, bool has_peers)
    EXPECT_EQ(fsm.on_scan_failed(true, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING);
}

// If a node in in PAIRING state and scan fails, but pairing timeout was reached,
// it will go to IDLE state
TEST_F(NodeStateMachineTest, ScanFailedInPairingWhilePairingExpiredTransitionsToIdle)
{
    fsm.on_init(false); // PAIRING
    // on_scan_failed(bool is_pairing_active, bool has_peers)
    EXPECT_EQ(fsm.on_scan_failed(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// If a node in in SCANNING state and scan fails, but pairing timeout was not
// reached, it will retry scan, staying in SCANNING to try again
TEST_F(NodeStateMachineTest, ScanFailedInScanningWhilePairingActiveTransitionsToScanning)
{
    fsm.on_init(false);      // PAIRING
    fsm.on_scan_requested(); // SCANNING
    EXPECT_EQ(fsm.on_scan_failed(true, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING); // Retry loop
}

// If a node is in SCANNING state and scan fails and pairing timeout expired without peers, is because
// the node was trying to pair but failed, so it goes to IDLE state
TEST_F(NodeStateMachineTest, ScanFailedInScanningWhilePairingExpiredWithoutPeersTransitionsToIdle)
{
    fsm.on_init(false);      // PAIRING
    fsm.on_scan_requested(); // SCANNING
    EXPECT_EQ(fsm.on_scan_failed(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// If a node is in SCANNING state and scan fails and pairing timeout expired with peers, is because
// the node already has peers and is trying to find a channel to restablish connection
TEST_F(NodeStateMachineTest, ScanFailedInScanningWhilePairingExpiredWithPeersTransitionsToOperational)
{
    fsm.on_init(true);       // OPERATIONAL
    fsm.on_scan_requested(); // SCANNING
    EXPECT_EQ(fsm.on_scan_failed(false, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

TEST_F(NodeStateMachineTest, InvalidTransitionsReturnErrorState)
{
    // UNINITIALIZED -> PAIRING is invalid (must init first)
    EXPECT_EQ(fsm.on_pairing_requested(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);

    // Redundant init
    fsm.on_init(false); // Valid init
    EXPECT_EQ(fsm.on_init(true), ESP_ERR_INVALID_STATE);

    // IDLE -> SCANNING is invalid (only from Operational or Pairing)
    fsm.on_pairing_completed(false); // Transition to IDLE
    ASSERT_EQ(fsm.get_state(), NodeState::IDLE);
    EXPECT_EQ(fsm.on_scan_requested(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);

    // Invalid transition for pairing requested (already in PAIRING)
    fsm.on_pairing_requested(); // Valid IDLE -> PAIRING
    EXPECT_EQ(fsm.on_pairing_requested(), ESP_ERR_INVALID_STATE);

    // Invalid transition for pairing completed (must be in PAIRING)
    fsm.on_pairing_completed(true); // Valid PAIRING -> OPERATIONAL
    EXPECT_EQ(fsm.on_pairing_completed(true), ESP_ERR_INVALID_STATE);

    // Invalid transition for channel found (must be in SCANNING or PAIRING)
    EXPECT_EQ(fsm.on_channel_found(false, false), ESP_ERR_INVALID_STATE);

    // Invalid scan failed from OPERATIONAL
    EXPECT_EQ(fsm.on_scan_failed(false, false), ESP_ERR_INVALID_STATE);
}
