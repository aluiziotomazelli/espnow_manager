#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_discovery_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_pairing_manager.hpp"

#include "message_router.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// ==========================================================================
// Test Fixture for MessageRouter
//
// The MessageRouter is responsible for dispatching incoming DecodedPackets
// to the appropriate specific manager (Pairing, Heartbeat, Discovery, TX)
// based on the message type in the header.
// ==========================================================================
class MessageRouterTest : public ::testing::Test
{
protected:
    // Mocks for dependencies
    NiceMock<MockDiscoveryManager> discovery_manager;
    NiceMock<MockTxManager> tx_manager;
    NiceMock<MockHeartbeatManager> heartbeat_manager;
    NiceMock<MockPairingManager> pairing_manager;

    // System Under Test
    MessageRouter router;

    MessageRouterTest()
        : router(discovery_manager, tx_manager, heartbeat_manager, pairing_manager)
    {
    }

    void SetUp() override
    {
        // Common setup if needed
    }

    // Helper to create a basic decoded packet
    DecodedPacket create_packet(MessageType type, size_t len = 0)
    {
        DecodedPacket packet = {};
        packet.header.msg_type = type;
        packet.raw.len = len;
        // Set defaults to avoid malformed packet checks if length is checked
        if (len == 0) {
            // Set reasonable defaults for length checks based on type if needed,
            // or tests can override
            packet.raw.len = 100;
        }
        else {
            packet.raw.len = len;
        }
        return packet;
    }
};

// ==========================================================================
// Basic Test to verify fixture setup
// ==========================================================================
TEST_F(MessageRouterTest, VerifyFixtureSetup)
{
    // Just checking if the object is constructed without crashing
    SUCCEED();
}

// ==========================================================================
// Pairing Tests
// ==========================================================================

TEST_F(MessageRouterTest, PairRequestCallsPairingManager)
{
    DecodedPacket packet = create_packet(MessageType::PAIR_REQUEST, sizeof(PairRequest));
    EXPECT_CALL(pairing_manager, handle_request(_)).Times(1);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, ShortPairRequestDoesNotCallPairingManager)
{
    DecodedPacket packet = create_packet(MessageType::PAIR_REQUEST, sizeof(PairRequest) - 1);
    EXPECT_CALL(pairing_manager, handle_request(_)).Times(0);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, PairResponseCallsHandleResponse)
{
    DecodedPacket packet = create_packet(MessageType::PAIR_RESPONSE, sizeof(PairResponse));
    EXPECT_CALL(pairing_manager, handle_response(_)).Times(1);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, ShortPairResponseDoesNotCallPairingManager)
{
    DecodedPacket packet = create_packet(MessageType::PAIR_RESPONSE, sizeof(PairResponse) - 1);
    EXPECT_CALL(pairing_manager, handle_response(_)).Times(0);
    router.handle_packet(packet);
}

// ==========================================================================
// Heartbeat
// ==========================================================================

TEST_F(MessageRouterTest, HeartbeatCallsHandleRequest)
{
    DecodedPacket packet = create_packet(MessageType::HEARTBEAT, sizeof(HeartbeatMessage));
    EXPECT_CALL(heartbeat_manager, handle_request(_)).Times(1);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, ShortHeartbeatDoesNotCallHandleRequest)
{
    DecodedPacket packet = create_packet(MessageType::HEARTBEAT, sizeof(HeartbeatMessage) - 1);
    EXPECT_CALL(heartbeat_manager, handle_request(_)).Times(0);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, HeartbeatResponseCallsHandleResponse)
{
    DecodedPacket packet = create_packet(MessageType::HEARTBEAT_RESPONSE, sizeof(HeartbeatResponse));
    EXPECT_CALL(heartbeat_manager, handle_response()).Times(1);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, ShortHeartbeatResponseDoesNotCallHandleResponse)
{
    DecodedPacket packet = create_packet(MessageType::HEARTBEAT_RESPONSE, sizeof(HeartbeatResponse) - 1);
    EXPECT_CALL(heartbeat_manager, handle_response()).Times(0);
    router.handle_packet(packet);
}

// ==========================================================================
// ACK
// ==========================================================================

TEST_F(MessageRouterTest, AckCallsNotifyLogicalAck)
{
    DecodedPacket packet = create_packet(MessageType::ACK, sizeof(AckMessage));
    EXPECT_CALL(tx_manager, notify_logical_ack()).Times(1);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, ShortAckDoesNotCallNotifyLogicalAck)
{
    DecodedPacket packet = create_packet(MessageType::ACK, sizeof(AckMessage) - 1);
    EXPECT_CALL(tx_manager, notify_logical_ack()).Times(0);
    router.handle_packet(packet);
}

// ==========================================================================
// Channel Scan
// ==========================================================================

TEST_F(MessageRouterTest, ChannelScanProbeCallsHandleProbe)
{
    DecodedPacket packet = create_packet(MessageType::CHANNEL_SCAN_PROBE, sizeof(MessageHeader));
    EXPECT_CALL(discovery_manager, handle_probe(_)).Times(1);
    router.handle_packet(packet);
}

TEST_F(MessageRouterTest, ShortChannelScanProbeDoesNotCallHandleProbe)
{
    DecodedPacket packet = create_packet(MessageType::CHANNEL_SCAN_PROBE, sizeof(MessageHeader) - 1);
    EXPECT_CALL(discovery_manager, handle_probe(_)).Times(0);
    router.handle_packet(packet);
}

// ==========================================================================
// Unknown MessageType
// ==========================================================================
TEST_F(MessageRouterTest, UnknownMessageTypeDoesNotCallAnyManager)
{
    DecodedPacket packet = create_packet(static_cast<MessageType>(999), sizeof(MessageHeader));
    EXPECT_CALL(discovery_manager, handle_probe(_)).Times(0);
    EXPECT_CALL(tx_manager, notify_logical_ack()).Times(0);
    EXPECT_CALL(heartbeat_manager, handle_request(_)).Times(0);
    EXPECT_CALL(heartbeat_manager, handle_response()).Times(0);
    EXPECT_CALL(pairing_manager, handle_request(_)).Times(0);
    EXPECT_CALL(pairing_manager, handle_response(_)).Times(0);
    router.handle_packet(packet);
}
