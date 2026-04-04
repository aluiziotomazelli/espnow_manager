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
    EXPECT_EQ(fsm.on_init(false, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// When initialized without peers and is not a HUB, it should start searching for a HUB (PAIRING_SCAN).
TEST_F(NodeStateMachineTest, InitWithoutPeersAndNotHubTransitionsToPairingScan)
{
    EXPECT_EQ(fsm.on_init(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// When initialized without peers and is a HUB, it should start pairing to receive nodes
TEST_F(NodeStateMachineTest, InitWithoutPeersAndIsHubTransitionsToPairing)
{
    EXPECT_EQ(fsm.on_init(true, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

TEST_F(NodeStateMachineTest, InitNotInUninitializedReturnsError)
{
    fsm.on_init(false, false);
    EXPECT_EQ(fsm.on_init(false, false), ESP_ERR_INVALID_STATE);
}

// ===================================
// 2. Pairing Requests
// ===================================

// Pairing requested in UNINITIALIZED state should return error.
TEST_F(NodeStateMachineTest, PairingRequestedInUninitializedReturnsError)
{
    EXPECT_EQ(fsm.on_pairing_requested(false, false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

// Pairing requested in PAIRING_SCAN state should return error.
TEST_F(NodeStateMachineTest, PairingRequestedInPairingScanReturnsError)
{
    fsm.on_init(false, false); // PAIRING_SCAN
    EXPECT_EQ(fsm.on_pairing_requested(false, false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// Pairing requested in RECOVERY_SCAN state should return error.
TEST_F(NodeStateMachineTest, PairingRequestedInRecoveryScanReturnsError)
{
    fsm.on_init(false, true);
    fsm.on_scan_requested(); // RECOVERY_SCAN
    EXPECT_EQ(fsm.on_pairing_requested(false, false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::RECOVERY_SCAN);
}

// Pairing requested in PAIRING state should return error.
TEST_F(NodeStateMachineTest, PairingRequestedInPairingReturnsError)
{
    fsm.on_init(false, false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_pairing_requested(false, false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// From IDLE (no peers), a non HUB should go to pairing scan
TEST_F(NodeStateMachineTest, PairingRequestedFromIdleWithoutPeersTransitionsToPairingScan)
{
    fsm.on_init(false, false); // Not a HUB, no peers -> PAIRING_SCAN
    fsm.on_scan_failed(); // Go to IDLE
    ASSERT_EQ(fsm.get_state(), NodeState::IDLE);

    EXPECT_EQ(fsm.on_pairing_requested(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// From IDLE (but has peers, e.g. after a recovery scan failed), pairing request can start directly.
TEST_F(NodeStateMachineTest, PairingRequestedFromIdleWithPeersTransitionsToPairing)
{
    fsm.on_init(true, false);
    fsm.on_scan_requested();
    fsm.on_scan_failed(); // Go to IDLE (per revised table)
    ASSERT_EQ(fsm.get_state(), NodeState::IDLE);

    EXPECT_EQ(fsm.on_pairing_requested(true, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// From OPERATIONAL (has peers), pairing request starts directly.
TEST_F(NodeStateMachineTest, PairingRequestedFromOperationalWithPeersTransitionsToPairing)
{
    fsm.on_init(false, true);
    ASSERT_EQ(fsm.get_state(), NodeState::OPERATIONAL);

    EXPECT_EQ(fsm.on_pairing_requested(true, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// Edge case: if in OPERATIONAL but all peers removed, pairing request needs scan.
TEST_F(NodeStateMachineTest, PairingRequestedFromOperationalWithoutPeersTransitionsToPairingScan)
{
    fsm.on_init(false, true);
    ASSERT_EQ(fsm.get_state(), NodeState::OPERATIONAL);

    EXPECT_EQ(fsm.on_pairing_requested(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// ===================================
// 3. Channel Scanning
// ===================================

// Scan requested in UNINITIALIZED state should return error.
TEST_F(NodeStateMachineTest, ScanRequestedInUninitializedReturnsError)
{
    EXPECT_EQ(fsm.on_scan_requested(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

// Scan requested in PAIRING_SCAN state should return error.
TEST_F(NodeStateMachineTest, ScanRequestedInPairingScanReturnsError)
{
    fsm.on_init(false, false); // PAIRING_SCAN
    EXPECT_EQ(fsm.on_scan_requested(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING_SCAN);
}

// Scan requested from PAIRING state transitions to RECOVERY_SCAN.
TEST_F(NodeStateMachineTest, ScanRequestedFromPairingTransitionsToRecoveryScan)
{
    fsm.on_init(false, false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::RECOVERY_SCAN);
}

// Scan requested from IDLE state transitions to RECOVERY_SCAN.
TEST_F(NodeStateMachineTest, ScanRequestedFromIdleTransitionsToRecoveryScan)
{
    fsm.on_init(false, false);
    fsm.on_scan_failed(); // IDLE
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::RECOVERY_SCAN);
}

// When link is lost in OPERATIONAL, it should start RECOVERY_SCAN.
TEST_F(NodeStateMachineTest, ScanRequestedFromOperationalTransitionsToRecoveryScan)
{
    fsm.on_init(false, true);
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::RECOVERY_SCAN);
}

// If channel found during PAIRING_SCAN, move to PAIRING.
TEST_F(NodeStateMachineTest, ChannelFoundInPairingScanTransitionsToPairing)
{
    fsm.on_init(false, false); // PAIRING_SCAN
    EXPECT_EQ(fsm.on_channel_found(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// If channel found during RECOVERY_SCAN, move back to OPERATIONAL.
TEST_F(NodeStateMachineTest, ChannelFoundInRecoveryScanTransitionsToOperational)
{
    fsm.on_init(false, true);
    fsm.on_scan_requested(); // RECOVERY_SCAN
    EXPECT_EQ(fsm.on_channel_found(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// Channel found in UNINITIALIZED state should return error.
TEST_F(NodeStateMachineTest, ChannelFoundInUninitializedReturnsError)
{
    EXPECT_EQ(fsm.on_channel_found(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

// Channel found in IDLE state should return error.
TEST_F(NodeStateMachineTest, ChannelFoundInIdleReturnsError)
{
    fsm.on_init(false, false);
    fsm.on_scan_failed(); // IDLE
    EXPECT_EQ(fsm.on_channel_found(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// Channel found in PAIRING state should return error.
TEST_F(NodeStateMachineTest, ChannelFoundInPairingReturnsError)
{
    fsm.on_init(false, false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_channel_found(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// ===================================
// 4. Scan Failures
// ===================================

// If scan for pairing fails, go to IDLE.
TEST_F(NodeStateMachineTest, ScanFailedInPairingScanTransitionsToIdle)
{
    fsm.on_init(false, false); // PAIRING_SCAN
    EXPECT_EQ(fsm.on_scan_failed(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// If scan for recovery fails, go to IDLE.
TEST_F(NodeStateMachineTest, ScanFailedInRecoveryScanTransitionsToIdle)
{
    fsm.on_init(false, true);
    fsm.on_scan_requested(); // RECOVERY_SCAN
    EXPECT_EQ(fsm.on_scan_failed(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// Scan failed in UNINITIALIZED state should return error.
TEST_F(NodeStateMachineTest, ScanFailedInUninitializedReturnsError)
{
    EXPECT_EQ(fsm.on_scan_failed(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

// Scan failed in OPERATIONAL state should return error.
TEST_F(NodeStateMachineTest, ScanFailedInOperationalReturnsError)
{
    fsm.on_init(false, true);
    EXPECT_EQ(fsm.on_scan_failed(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// Scan failed in PAIRING state should return error.
TEST_F(NodeStateMachineTest, ScanFailedInPairingReturnsError)
{
    fsm.on_init(false, false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_scan_failed(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

// Scan failed in IDLE state should return error.
TEST_F(NodeStateMachineTest, ScanFailedInIdleReturnsError)
{
    fsm.on_init(false, false);
    fsm.on_scan_failed(); // IDLE
    EXPECT_EQ(fsm.on_scan_failed(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// ===================================
// 5. Pairing Completion
// ===================================

// Pairing timeout in UNINITIALIZED state should return error.
TEST_F(NodeStateMachineTest, PairingTimeoutInUninitializedReturnsError)
{
    EXPECT_EQ(fsm.on_pairing_timeout(false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

// Pairing timeout in OPERATIONAL state should return error.
TEST_F(NodeStateMachineTest, PairingTimeoutInOperationalReturnsError)
{
    fsm.on_init(false, true);
    EXPECT_EQ(fsm.on_pairing_timeout(false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// Pairing timeout in IDLE state should return error.
TEST_F(NodeStateMachineTest, PairingTimeoutInIdleReturnsError)
{
    fsm.on_init(false, false);
    fsm.on_scan_failed(); // IDLE
    EXPECT_EQ(fsm.on_pairing_timeout(false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// Successful pairing (has_peers = true) leads to OPERATIONAL.
TEST_F(NodeStateMachineTest, PairingSucceededTransitionsToOperational)
{
    fsm.on_init(false, false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_pairing_timeout(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// Failed pairing without existing peers leads to IDLE.
TEST_F(NodeStateMachineTest, PairingFailedWithoutPeersTransitionsToIdle)
{
    fsm.on_init(false, false);
    fsm.on_channel_found(); // PAIRING
    EXPECT_EQ(fsm.on_pairing_timeout(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

// Failed pairing WITH existing peers (e.g. adding a second node failed) returns to OPERATIONAL.
TEST_F(NodeStateMachineTest, PairingFailedWithPeersTransitionsToOperational)
{
    fsm.on_init(false, true);
    fsm.on_pairing_requested(true, true); // PAIRING
    EXPECT_EQ(fsm.on_pairing_timeout(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

// ===================================
// 6. Lifecycle
// ===================================

TEST_F(NodeStateMachineTest, DeinitTransitionsToUninitialized)
{
    fsm.on_init(false, false);
    EXPECT_EQ(fsm.on_deinit(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

TEST_F(NodeStateMachineTest, ResetTransitionsToUninitialized)
{
    fsm.on_init(false, false);
    fsm.reset();
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}
