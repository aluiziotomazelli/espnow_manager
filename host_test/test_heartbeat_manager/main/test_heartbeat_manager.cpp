// host_test/test_heartbeat_manager/main/test_heartbeat_manager.cpp
#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_hal_timer.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"

#include "heartbeat_manager.hpp"
#include "protocol_messages.hpp"

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// Testable subclass — exposes protected send_heartbeat()
// ---------------------------------------------------------------------------
class TestableHeartbeatManager : public HeartbeatManager
{
public:
    using HeartbeatManager::HeartbeatManager;
    void force_send_heartbeat() { send_heartbeat(); }
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class HeartbeatManagerTest : public ::testing::Test
{
protected:
    static constexpr NodeId kNodeId = 0x02;
    static constexpr NodeType kNodeType = 0x02; // non-HUB

    NiceMock<MockTxManager> tx_mgr_;
    NiceMock<MockPeerManager> peer_mgr_;
    NiceMock<MockTimerHAL> hal_timer_;

    std::unique_ptr<HeartbeatManager> sut_;
    std::unique_ptr<TestableHeartbeatManager> sut_testable_;

    void SetUp() override
    {
        ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
        ON_CALL(peer_mgr_, find_mac(_, _)).WillByDefault(Return(false));

        sut_ = std::make_unique<HeartbeatManager>(tx_mgr_, peer_mgr_, hal_timer_);
        sut_testable_ = std::make_unique<TestableHeartbeatManager>(tx_mgr_, peer_mgr_, hal_timer_);
    }

    // -----------------------------------------------------------------------
    // Helper: build a minimal DecodedRxPacket carrying a HEARTBEAT from a node.
    // -----------------------------------------------------------------------
    static DecodedRxPacket make_decoded_packet(NodeId sender_id, size_t len_override = 0)
    {
        DecodedRxPacket decoded{};
        decoded.raw.len = (len_override > 0) ? len_override : sizeof(HeartbeatMessage);
        decoded.header.msg_type = MessageType::HEARTBEAT;
        decoded.header.sender_node_id = sender_id;
        memcpy(decoded.raw.src_mac, "\xAA\xBB\xCC\xDD\xEE\xFF", 6);
        return decoded;
    }
};

// ===========================================================================
// init()
// ===========================================================================

TEST_F(HeartbeatManagerTest, InitReturnsOk)
{
    sut_->init(kNodeId, kNodeType, 1000);
}

TEST_F(HeartbeatManagerTest, NotInitializedBeforeInit)
{
    // tick() should be a no-op before init()
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(999999);
}

// ===========================================================================
// tick()
// ===========================================================================

TEST_F(HeartbeatManagerTest, TickHubDoesNotSendHeartbeat)
{
    sut_->init(ReservedIds::HUB, ReservedTypes::HUB, 1000);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(999999);
}

TEST_F(HeartbeatManagerTest, TickBeforeIntervalDoesNotSend)
{
    sut_->init(kNodeId, kNodeType, 5000);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(4999); // just below interval
}

TEST_F(HeartbeatManagerTest, TickAtIntervalSendsHeartbeat)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    sut_->init(kNodeId, kNodeType, 5000);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);
    sut_->tick(5000); // exactly at interval
}

TEST_F(HeartbeatManagerTest, TickAfterIntervalSendsHeartbeat)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    sut_->init(kNodeId, kNodeType, 5000);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);
    sut_->tick(6000); // past interval
}

TEST_F(HeartbeatManagerTest, TickUpdatesLastHeartbeatTimeAfterSend)
{
    // After sending at t=5000, next tick at t=9999 should NOT send again
    // (interval is 5000ms, so next send is at t=10000)
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(tx_mgr_, queue_packet(_)).WillByDefault(Return(ESP_OK));

    sut_->init(kNodeId, kNodeType, 5000);
    sut_->tick(5000); // first send

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(9999); // not yet time for second send
}

TEST_F(HeartbeatManagerTest, TickSendsAgainAfterSecondInterval)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(tx_mgr_, queue_packet(_)).WillByDefault(Return(ESP_OK));

    sut_->init(kNodeId, kNodeType, 5000);
    sut_->tick(5000); // first send

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);
    sut_->tick(10000); // second interval reached
}

// ===========================================================================
// set_interval_ms()
// ===========================================================================

TEST_F(HeartbeatManagerTest, OriginalIntervalFiresBeforeSetInterval)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    sut_->init(kNodeId, kNodeType, 5000);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);
    sut_->tick(6000); // fires at original 5000ms interval
}

TEST_F(HeartbeatManagerTest, SetIntervalIsRespectedOnNextTick)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    ON_CALL(tx_mgr_, queue_packet(_)).WillByDefault(Return(ESP_OK));

    sut_->init(kNodeId, kNodeType, 5000);
    sut_->tick(6000); // consume first send silently via ON_CALL

    sut_->set_interval_ms(1000);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);
    sut_->tick(7000); // 1000ms after last send at t=6000 — fires with new interval
}

// ===========================================================================
// handle_request()
// ===========================================================================

TEST_F(HeartbeatManagerTest, HandleRequestValidUpdatesLastSeenAndQueuesResponse)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(5000000)); // 5s
    sut_->init(ReservedIds::HUB, ReservedTypes::HUB, 0);

    auto pkt = make_decoded_packet(kNodeId);

    EXPECT_CALL(peer_mgr_, update_last_seen(kNodeId, 5000));
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);

    sut_->handle_request(pkt);
}

TEST_F(HeartbeatManagerTest, HandleRequestResponseHeaderIsCorrect)
{
    sut_->init(ReservedIds::HUB, ReservedTypes::HUB, 0);

    auto pkt = make_decoded_packet(kNodeId);

    DecodedTxPacket captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Invoke([&](const DecodedTxPacket& p) -> esp_err_t {
        captured = p;
        return ESP_OK;
    }));

    sut_->handle_request(pkt);

    EXPECT_EQ(MessageType::HEARTBEAT_RESPONSE, captured.header.msg_type);
    EXPECT_EQ(ReservedIds::HUB, captured.header.sender_node_id);
    EXPECT_EQ(kNodeId, captured.header.dest_node_id);
}

// ===========================================================================
// handle_response() — RSSI capture
// ===========================================================================

TEST_F(HeartbeatManagerTest, HandleResponseCapturesRssiFromPacket)
{
    sut_testable_->init(kNodeId, kNodeType, 0);

    // Simulate a heartbeat response packet with a specific RSSI value
    DecodedRxPacket decoded{};
    decoded.raw.rssi = -65; // dBm

    sut_testable_->handle_response(decoded);

    // Verify RSSI is included in the next heartbeat
    HeartbeatMessage captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Invoke([&](const DecodedTxPacket& p) -> esp_err_t {
        // Payload starts at uptime_ms, so copy into the payload portion of captured
        memcpy(&captured.uptime_ms, p.payload, p.payload_len);
        return ESP_OK;
    }));

    sut_testable_->force_send_heartbeat();

    EXPECT_EQ(-65, captured.rssi);
}

// ===========================================================================
// deinit()
// ===========================================================================

TEST_F(HeartbeatManagerTest, DeinitDisablesHeartbeatSending)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    sut_->init(kNodeId, kNodeType, 5000);

    // First tick sends heartbeat
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(1);
    sut_->tick(5000);
}

TEST_F(HeartbeatManagerTest, TickAfterDeinitDoesNotSend)
{
    ON_CALL(hal_timer_, get_time_us()).WillByDefault(Return(0));
    sut_->init(kNodeId, kNodeType, 5000);

    // Deinit should disable heartbeat sending
    sut_->deinit();

    // After deinit, tick() should not call queue_packet because is_initialized_ is false
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(5000);
}

// ===========================================================================
// send_heartbeat() — via TestableHeartbeatManager
// ===========================================================================

TEST_F(HeartbeatManagerTest, SendHeartbeatHubUnknownUsesBroadcastMac)
{
    ON_CALL(peer_mgr_, find_mac(ReservedIds::HUB, _)).WillByDefault(Return(false));
    sut_testable_->init(kNodeId, kNodeType, 0);

    DecodedTxPacket captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Invoke([&](const DecodedTxPacket& p) -> esp_err_t {
        captured = p;
        return ESP_OK;
    }));

    sut_testable_->force_send_heartbeat();

    EXPECT_EQ(0, memcmp(BROADCAST_MAC, captured.dest_mac, 6));
}

TEST_F(HeartbeatManagerTest, SendHeartbeatHubKnownUsesUnicastMac)
{
    const uint8_t hub_mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    ON_CALL(peer_mgr_, find_mac(ReservedIds::HUB, _)).WillByDefault(Invoke([&](NodeId, uint8_t* out) {
        memcpy(out, hub_mac, 6);
        return true;
    }));

    sut_testable_->init(kNodeId, kNodeType, 0);

    DecodedTxPacket captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Invoke([&](const DecodedTxPacket& p) -> esp_err_t {
        captured = p;
        return ESP_OK;
    }));

    sut_testable_->force_send_heartbeat();

    EXPECT_EQ(0, memcmp(hub_mac, captured.dest_mac, 6));
}

TEST_F(HeartbeatManagerTest, SendHeartbeatHeaderIsCorrect)
{
    sut_testable_->init(kNodeId, kNodeType, 0);

    DecodedTxPacket captured{};
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Invoke([&](const DecodedTxPacket& p) -> esp_err_t {
        captured = p;
        return ESP_OK;
    }));

    sut_testable_->force_send_heartbeat();

    EXPECT_EQ(MessageType::HEARTBEAT, captured.header.msg_type);
    EXPECT_EQ(kNodeId, captured.header.sender_node_id);
    EXPECT_EQ(ReservedIds::HUB, captured.header.dest_node_id);
}
