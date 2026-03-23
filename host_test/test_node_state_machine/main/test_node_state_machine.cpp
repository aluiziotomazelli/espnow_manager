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

TEST_F(NodeStateMachineTest, InitialStateIsUninitialized)
{
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);
}

TEST_F(NodeStateMachineTest, InitSuccessWithoutPeersTransitionsToIdle)
{
    EXPECT_EQ(fsm.on_init(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

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

TEST_F(NodeStateMachineTest, PairingRequestedFromIdleTransitionsToPairing)
{
    fsm.on_init(false);
    EXPECT_EQ(fsm.on_pairing_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

TEST_F(NodeStateMachineTest, PairingRequestedFromOperationalTransitionsToPairing)
{
    fsm.on_init(true);
    EXPECT_EQ(fsm.on_pairing_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

TEST_F(NodeStateMachineTest, PairingCompletedWithPeersTransitionsToOperational)
{
    fsm.on_init(false);
    fsm.on_pairing_requested();
    EXPECT_EQ(fsm.on_pairing_completed(true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

TEST_F(NodeStateMachineTest, PairingCompletedWithoutPeersTransitionsToIdle)
{
    fsm.on_init(false);
    fsm.on_pairing_requested();
    EXPECT_EQ(fsm.on_pairing_completed(false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

TEST_F(NodeStateMachineTest, ScanRequestedFromOperationalTransitionsToScanning)
{
    fsm.on_init(true);
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING);
}

TEST_F(NodeStateMachineTest, ScanRequestedFromPairingTransitionsToScanning)
{
    fsm.on_init(false);
    fsm.on_pairing_requested();
    EXPECT_EQ(fsm.on_scan_requested(), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING);
}

TEST_F(NodeStateMachineTest, ChannelFoundByHubTransitionsToOperational)
{
    fsm.on_init(true);
    fsm.on_scan_requested();
    // HUB always returns to OPERATIONAL
    EXPECT_EQ(fsm.on_channel_found(true, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

TEST_F(NodeStateMachineTest, ChannelFoundByPeripheralWithPeersTransitionsToOperational)
{
    fsm.on_init(true);
    fsm.on_scan_requested();
    EXPECT_EQ(fsm.on_channel_found(false, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

TEST_F(NodeStateMachineTest, ChannelFoundByPeripheralWithoutPeersTransitionsToPairing)
{
    fsm.on_init(false);
    fsm.on_pairing_requested(); // This might trigger a scan internally in manager
    fsm.on_scan_requested();
    EXPECT_EQ(fsm.on_channel_found(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::PAIRING);
}

TEST_F(NodeStateMachineTest, ScanFailedWhilePairingActiveTransitionsToScanning)
{
    fsm.on_init(false);
    fsm.on_pairing_requested();
    fsm.on_scan_requested();
    EXPECT_EQ(fsm.on_scan_failed(true, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::SCANNING); // Retry loop
}

TEST_F(NodeStateMachineTest, ScanFailedWhilePairingExpiredTransitionsToIdle)
{
    fsm.on_init(false);
    fsm.on_pairing_requested();
    fsm.on_scan_requested();
    EXPECT_EQ(fsm.on_scan_failed(false, false), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}

TEST_F(NodeStateMachineTest, ScanFailedFromScanningWithPeersTransitionsToOperational)
{
    fsm.on_init(true);
    fsm.on_scan_requested();
    EXPECT_EQ(fsm.on_scan_failed(false, true), ESP_OK);
    EXPECT_EQ(fsm.get_state(), NodeState::OPERATIONAL);
}

TEST_F(NodeStateMachineTest, InvalidTransitionsReturnErrorState)
{
    // UNINITIALIZED -> PAIRING is invalid (must init first)
    EXPECT_EQ(fsm.on_pairing_requested(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::UNINITIALIZED);

    // IDLE -> SCANNING is invalid (only from Operational or Pairing)
    fsm.on_init(false);
    EXPECT_EQ(fsm.on_scan_requested(), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(fsm.get_state(), NodeState::IDLE);
}
