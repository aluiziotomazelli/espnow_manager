// host_test/test_pairing_manager/main/test_pairing_manager.cpp
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_message_codec.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_hal_freertos.hpp"

#include "pairing_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SaveArg;
using ::testing::SetArgPointee;

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr NodeId kNodeId = 0x02;
static constexpr NodeType kNodeType = 0x02; // non-HUB
static constexpr NodeId kHubId = ReservedIds::HUB;
static constexpr NodeType kHubType = ReservedTypes::HUB;
static constexpr uint64_t kT0 = 1000; // arbitrary start timestamp

// Fake TaskHandle for tests
static int fake_task_storage = 0;
static TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(&fake_task_storage);

// ---------------------------------------------------------------------------
// Testable PairingManager — exposes protected is_active_ for testing
// ---------------------------------------------------------------------------
class TestablePairingManager : public PairingManager
{
public:
    TestablePairingManager(ITxManager& tx_mgr, IPeerManager& peer_mgr, IFreeRTOSHAL& hal_freertos)
        : PairingManager(tx_mgr, peer_mgr, hal_freertos)
    {
    }

    bool get_is_active() const { return is_active_; }
    void set_is_active(bool active) { is_active_ = active; }
};

// ---------------------------------------------------------------------------
// Fixture — non-HUB node
//
// NiceMock rationale: codec_.encode() and tx_mgr_.queue_packet() are called
// inside send_pair_request(), which fires on start() and on every tick()
// interval. Tests focused on state transitions (is_active_, timeout)
// should not fail on those incidental transmissions — NiceMock silences them.
// Tests that explicitly verify transmission behaviour use EXPECT_CALL directly.
// ---------------------------------------------------------------------------
class PairingManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockTxManager> tx_mgr_;
    NiceMock<MockPeerManager> peer_mgr_;
    NiceMock<MockFreeRTOSHAL> hal_freertos_;
    NiceMock<MockMessageCodec> codec_; // Kept for Mock setup if needed by helper

    std::unique_ptr<TestablePairingManager> sut_;

    void SetUp() override
    {
        sut_ = std::make_unique<TestablePairingManager>(tx_mgr_, peer_mgr_, hal_freertos_);
        sut_->init(kNodeId, kNodeType, fake_rx_task);
    }

    // -----------------------------------------------------------------------
    // Helper: DecodedRxPacket carrying a PairResponse from the HUB.
    // -----------------------------------------------------------------------
    static DecodedRxPacket make_decoded_pair_response(PairStatus status = PairStatus::ACCEPTED)
    {
        DecodedRxPacket decoded{};
        auto* resp = reinterpret_cast<PairResponse*>(decoded.raw.data);
        resp->header.msg_type = MessageType::PAIR_RESPONSE;
        resp->header.sender_node_id = kHubId;
        resp->header.sender_type = kHubType;
        resp->header.dest_node_id = kNodeId;
        resp->header.sequence_number = 0;
        resp->status = status;
        decoded.raw.len = sizeof(PairResponse);

        decoded.header = resp->header;
        return decoded;
    }

    // -----------------------------------------------------------------------
    // Helper: DecodedRxPacket carrying a PairRequest from an arbitrary node.
    // -----------------------------------------------------------------------
    static DecodedRxPacket make_decoded_pair_request(NodeId sender_id, NodeType sender_type)
    {
        DecodedRxPacket decoded{};
        auto* req = reinterpret_cast<PairRequest*>(decoded.raw.data);
        req->header.msg_type = MessageType::PAIR_REQUEST;
        req->header.sender_node_id = sender_id;
        req->header.sender_type = sender_type;
        req->header.dest_node_id = kHubId;
        req->header.sequence_number = 0;
        req->heartbeat_interval_ms = 60000;
        decoded.raw.len = sizeof(PairRequest);

        decoded.header = req->header;
        return decoded;
    }
};

// ---------------------------------------------------------------------------
// Fixture — HUB role
// Same mocks, constructed with HUB id/type. Kept separate so test names
// clearly communicate which role is under test.
// ---------------------------------------------------------------------------
class PairingManagerHubTest : public ::testing::Test
{
protected:
    NiceMock<MockTxManager> tx_mgr_;
    NiceMock<MockPeerManager> peer_mgr_;
    NiceMock<MockFreeRTOSHAL> hal_freertos_;
    NiceMock<MockMessageCodec> codec_;

    std::unique_ptr<TestablePairingManager> sut_;

    void SetUp() override
    {
        sut_ = std::make_unique<TestablePairingManager>(tx_mgr_, peer_mgr_, hal_freertos_);
        sut_->init(kHubId, kHubType, fake_rx_task);
    }

    static DecodedRxPacket make_decoded_pair_request(NodeId sender_id, NodeType sender_type)
    {
        DecodedRxPacket decoded{};
        auto* req = reinterpret_cast<PairRequest*>(decoded.raw.data);
        req->header.msg_type = MessageType::PAIR_REQUEST;
        req->header.sender_node_id = sender_id;
        req->header.sender_type = sender_type;
        req->header.dest_node_id = kHubId;
        req->header.sequence_number = 0;
        req->heartbeat_interval_ms = 60000;
        decoded.raw.len = sizeof(PairRequest);

        decoded.header = req->header;
        return decoded;
    }
};

// ===========================================================================
// init()
// ===========================================================================

TEST_F(PairingManagerTest, InitReturnsOk)
{
    TestablePairingManager pm(tx_mgr_, peer_mgr_, hal_freertos_);
    EXPECT_EQ(pm.init(kNodeId, kNodeType, fake_rx_task), ESP_OK);
}

TEST_F(PairingManagerTest, InitReturnsInvalidArgIfRxTaskHandleIsNull)
{
    TestablePairingManager pm(tx_mgr_, peer_mgr_, hal_freertos_);
    // Pass nullptr as rx_task_handle — should return ESP_ERR_INVALID_ARG
    EXPECT_EQ(pm.init(kNodeId, kNodeType, nullptr), ESP_ERR_INVALID_ARG);
}

TEST_F(PairingManagerTest, InitReturnsInvalidStateIfAlreadyInitialized)
{
    // sut_ is already initialized in SetUp()
    EXPECT_EQ(sut_->init(kNodeId, kNodeType, fake_rx_task), ESP_ERR_INVALID_STATE);
}

TEST_F(PairingManagerTest, NotActiveAfterInit)
{
    EXPECT_FALSE(sut_->get_is_active());
}

// ===========================================================================
// deinit()
// ===========================================================================

TEST_F(PairingManagerTest, DeinitClearsInitializedState)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    EXPECT_TRUE(sut_->get_is_active());

    sut_->deinit();

    // After deinit, re-init should succeed (not return INVALID_STATE)
    EXPECT_EQ(sut_->init(kNodeId, kNodeType, fake_rx_task), ESP_OK);
}

TEST_F(PairingManagerTest, DeinitDeactivatesPairing)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    EXPECT_TRUE(sut_->get_is_active());

    sut_->deinit();

    EXPECT_FALSE(sut_->get_is_active());
}

TEST_F(PairingManagerTest, DeinitClearsRxTaskHandle)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    // After deinit, task_notify should not be called because rx_task_handle_ is null
    EXPECT_CALL(hal_freertos_, task_notify(_, _, _)).Times(0);

    sut_->deinit();

    // Trigger a scenario that would normally notify rx_task
    auto decoded = make_decoded_pair_response(PairStatus::ACCEPTED);
    sut_->init(kNodeId, kNodeType, fake_rx_task);
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    sut_->deinit();
    sut_->handle_response(decoded);
}

// ===========================================================================
// start()
// ===========================================================================

TEST_F(PairingManagerTest, StartFailsIfNotInitialized)
{
    TestablePairingManager pm(tx_mgr_, peer_mgr_, hal_freertos_);
    // init() not called — is_initialized_ is false
    EXPECT_EQ(pm.start(PAIRING_TIMEOUT_MS, kT0), ESP_ERR_INVALID_STATE);
}

TEST_F(PairingManagerTest, StartFailsIfAlreadyActive)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    EXPECT_EQ(sut_->start(PAIRING_TIMEOUT_MS, kT0), ESP_ERR_INVALID_STATE);
}

TEST_F(PairingManagerTest, StartActivatesPairing)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    EXPECT_TRUE(sut_->get_is_active());
}

TEST_F(PairingManagerTest, StartSendsInitialPairRequest)
{
    // Non-HUB node must send a pair request immediately on start()
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK));

    sut_->start(PAIRING_TIMEOUT_MS, kT0);
}

TEST_F(PairingManagerHubTest, StartDoesNotSendPairRequest)
{
    // HUB never sends pair requests — it only responds to them
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);

    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    EXPECT_TRUE(sut_->get_is_active());
}

// ===========================================================================
// tick()
// ===========================================================================

TEST_F(PairingManagerTest, TickDoesNothingIfNotActive)
{
    // No start() called — tick() should be a no-op
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(kT0 + PAIRING_TIMEOUT_MS + 1);
    EXPECT_FALSE(sut_->get_is_active());
}

TEST_F(PairingManagerTest, TickDoesNothingIfNotInitialized)
{
    TestablePairingManager pm(tx_mgr_, peer_mgr_, hal_freertos_);
    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    pm.tick(kT0 + PAIRING_TIMEOUT_MS + 1);
}

TEST_F(PairingManagerTest, TickTimesOutWhenTimeoutExceeded)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    EXPECT_TRUE(sut_->get_is_active());

    sut_->tick(kT0 + PAIRING_TIMEOUT_MS);
    EXPECT_FALSE(sut_->get_is_active());
}

TEST_F(PairingManagerTest, TickDoesNotTimeOutBeforeTimeout)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    sut_->tick(kT0 + PAIRING_TIMEOUT_MS - 1);
    EXPECT_TRUE(sut_->get_is_active());
}

TEST_F(PairingManagerTest, TickSendsPeriodicRequestAfterInterval)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    // Expect one periodic send after interval elapses
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK));

    sut_->tick(kT0 + PAIRING_PERIODIC_INTERVAL_MS);
}

TEST_F(PairingManagerTest, TickDoesNotSendBeforeInterval)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(kT0 + PAIRING_PERIODIC_INTERVAL_MS - 1);
}

TEST_F(PairingManagerTest, TickUpdatesLastRequestTimeAfterPeriodicSend)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);
    sut_->tick(kT0 + PAIRING_PERIODIC_INTERVAL_MS); // first periodic send

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(kT0 + PAIRING_PERIODIC_INTERVAL_MS * 2 - 1); // not yet time for second send
}

TEST_F(PairingManagerHubTest, TickDoesNotSendPeriodicRequest)
{
    // HUB never sends pair requests periodically
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    EXPECT_CALL(tx_mgr_, queue_packet(_)).Times(0);
    sut_->tick(kT0 + PAIRING_PERIODIC_INTERVAL_MS);
}

// ===========================================================================
// handle_response() — non-HUB node
// ===========================================================================

TEST_F(PairingManagerTest, HandleResponseIgnoredIfNotActive)
{
    // Pairing not started — response must be ignored
    EXPECT_CALL(peer_mgr_, add(_, _, _, _)).Times(0);

    auto decoded = make_decoded_pair_response();
    sut_->handle_response(decoded);
}

TEST_F(PairingManagerTest, HandleResponseIgnoredIfNotInitialized)
{
    TestablePairingManager pm(tx_mgr_, peer_mgr_, hal_freertos_);
    // In actual implementation, it returns early before any logic if not initialized
    auto decoded = make_decoded_pair_response();
    pm.handle_response(decoded);
}

TEST_F(PairingManagerTest, HandleResponseAcceptedDeactivatesPairing)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    auto decoded = make_decoded_pair_response(PairStatus::ACCEPTED);
    sut_->handle_response(decoded);

    EXPECT_FALSE(sut_->get_is_active());
}

TEST_F(PairingManagerTest, HandleResponseAcceptedNotifiesRxTask)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    // When pairing succeeds, notify_rx_task_pairing_done() should be called
    // which calls hal_freertos_.task_notify(rx_task_handle_, NOTIFY_PAIRING_DONE, _)
    EXPECT_CALL(hal_freertos_, task_notify(fake_rx_task, NOTIFY_PAIRING_DONE, _)).Times(1).WillOnce(Return(pdPASS));

    auto decoded = make_decoded_pair_response(PairStatus::ACCEPTED);
    sut_->handle_response(decoded);
}

TEST_F(PairingManagerTest, HandleResponseAcceptedAddsPeer)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    EXPECT_CALL(peer_mgr_, add(kHubId, _, kHubType, _)).Times(1);

    auto decoded = make_decoded_pair_response(PairStatus::ACCEPTED);
    sut_->handle_response(decoded);
}

TEST_F(PairingManagerTest, HandleResponseRejectedKeepsPairingActive)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    auto decoded = make_decoded_pair_response(PairStatus::REJECTED_NOT_ALLOWED);
    sut_->handle_response(decoded);

    EXPECT_TRUE(sut_->get_is_active());
}

TEST_F(PairingManagerHubTest, HandleResponseIgnoredByHub)
{
    // HUB never processes pair responses
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    EXPECT_CALL(peer_mgr_, add(_, _, _, _)).Times(0);

    auto decoded =
        make_decoded_pair_request(kNodeId, kNodeType); // Wrong type for response but HUB should ignore anyway
    sut_->handle_response(decoded);
    EXPECT_TRUE(sut_->get_is_active());
}

// ===========================================================================
// handle_request() — HUB
// ===========================================================================

TEST_F(PairingManagerHubTest, HandleRequestIgnoredIfNotActive)
{
    auto decoded = make_decoded_pair_request(kNodeId, kNodeType);
    sut_->handle_request(decoded);
}

TEST_F(PairingManagerHubTest, HandleRequestIgnoredIfNotInitialized)
{
    TestablePairingManager pm(tx_mgr_, peer_mgr_, hal_freertos_);
    auto decoded = make_decoded_pair_request(kNodeId, kNodeType);
    pm.handle_request(decoded);
}

TEST_F(PairingManagerHubTest, HandleRequestFromNodeAddsPeerAndResponds)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    EXPECT_CALL(peer_mgr_, add(kNodeId, _, kNodeType, _)).Times(1);
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK));

    auto decoded = make_decoded_pair_request(kNodeId, kNodeType);
    sut_->handle_request(decoded);
}

TEST_F(PairingManagerHubTest, HandleRequestFromHubIsRejected)
{
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    // HUB sender must NOT be added as peer
    EXPECT_CALL(peer_mgr_, add(_, _, _, _)).Times(0);

    // Response is still sent (REJECTED_NOT_ALLOWED)
    EXPECT_CALL(tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK));

    auto decoded = make_decoded_pair_request(kHubId, kHubType);
    sut_->handle_request(decoded);
}

TEST_F(PairingManagerTest, HandleRequestIgnoredByNonHub)
{
    // Non-HUB nodes must never process pair requests
    sut_->start(PAIRING_TIMEOUT_MS, kT0);

    auto decoded = make_decoded_pair_request(kNodeId, kNodeType);
    sut_->handle_request(decoded);
}
