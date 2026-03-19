// host_test/test_espnow_manager/main/test_espnow_manager.cpp
//
// Synchronous tests — no real tasks are created. The bootstrapper mock
// returns ESP_OK without spawning any task, so all tested behaviour is
// exercised through direct method calls on the SUT.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_bootstrapper.hpp"
#include "mock_discovery_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_message_codec.hpp"
#include "mock_message_router.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_tx_state_machine.hpp"

#include "espnow_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgReferee;

// ---------------------------------------------------------------------------
// Fake FreeRTOS handles
//
// Opaque non-null pointers — the SUT forwards them to HAL calls but never
// dereferences them directly, so any non-null address is sufficient.
// ---------------------------------------------------------------------------
static int fake_queue_storage = 0;
static int fake_worker_storage = 0;
static int fake_mutex_storage = 0;
static int fake_rx_task_storage = 0;
static int fake_worker_task_storage = 0;

static QueueHandle_t fake_queue = reinterpret_cast<QueueHandle_t>(&fake_queue_storage);
static QueueHandle_t fake_worker = reinterpret_cast<QueueHandle_t>(&fake_worker_storage);
static SemaphoreHandle_t fake_mutex = reinterpret_cast<SemaphoreHandle_t>(&fake_mutex_storage);
static TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(&fake_rx_task_storage);
static TaskHandle_t fake_worker_task = reinterpret_cast<TaskHandle_t>(&fake_worker_task_storage);

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr NodeId kNodeId = 0x05;
static constexpr NodeType kNodeType = 0x02; // non-HUB
static constexpr NodeId kHubId = ReservedIds::HUB;
static constexpr NodeType kHubType = ReservedTypes::HUB;
static constexpr PayloadType kPayloadType = 0x03;
static constexpr CommandType kCommandType = CommandType::REBOOT;

// ---------------------------------------------------------------------------
// Helper: minimal valid EspNowConfig
// app_rx_queue must be non-null to pass the guard in init().
// ---------------------------------------------------------------------------
static EspNowConfig make_valid_config()
{
    EspNowConfig cfg{};
    cfg.node_id = kNodeId;
    cfg.node_type = kNodeType;
    cfg.wifi_channel = 1;
    cfg.app_rx_queue = fake_queue;
    return cfg;
}

// ---------------------------------------------------------------------------
// Fixture
//
// NiceMock rationale: init() and deinit() trigger many incidental calls on
// submódules (heartbeat init, pairing init, scanner init, etc.). Tests that
// focus on NodeState transitions or guard conditions should not fail on those
// calls. Tests that explicitly verify delegation use EXPECT_CALL directly.
//
// Ownership pattern: each mock is heap-allocated, the raw pointer is saved
// for test access, then ownership is transferred to sut_ via unique_ptr.
// The mocks are valid for the lifetime of sut_ since sut_ owns them.
// ---------------------------------------------------------------------------
class EspNowManagerTest : public ::testing::Test
{
protected:
    // Raw pointers for test access — lifetime managed by sut_
    NiceMock<MockWiFiHAL> *hal_wifi_;
    NiceMock<MockTimerHAL> *hal_timer_;
    NiceMock<MockFreeRTOSHAL> *hal_freertos_;
    NiceMock<MockBootstrapper> *bootstrapper_;
    NiceMock<MockPeerManager> *peer_mgr_;
    NiceMock<MockMessageCodec> *codec_;
    NiceMock<MockDiscoveryManager> *scanner_;
    NiceMock<MockTxStateMachine> *tx_fsm_;
    NiceMock<MockTxManager> *tx_mgr_;
    NiceMock<MockHeartbeatManager> *heartbeat_mgr_;
    NiceMock<MockPairingManager> *pairing_mgr_;
    NiceMock<MockMessageRouter> *message_router_;

    std::unique_ptr<EspNowManager> sut_;

    void SetUp() override
    {
        auto hal_wifi = std::make_unique<NiceMock<MockWiFiHAL>>();
        auto hal_timer = std::make_unique<NiceMock<MockTimerHAL>>();
        auto hal_freertos = std::make_unique<NiceMock<MockFreeRTOSHAL>>();
        auto bootstrapper = std::make_unique<NiceMock<MockBootstrapper>>();
        auto peer_mgr = std::make_unique<NiceMock<MockPeerManager>>();
        auto codec = std::make_unique<NiceMock<MockMessageCodec>>();
        auto scanner = std::make_unique<NiceMock<MockDiscoveryManager>>();
        auto tx_fsm = std::make_unique<NiceMock<MockTxStateMachine>>();
        auto tx_mgr = std::make_unique<NiceMock<MockTxManager>>();
        auto heartbeat_mgr = std::make_unique<NiceMock<MockHeartbeatManager>>();
        auto pairing_mgr = std::make_unique<NiceMock<MockPairingManager>>();
        auto message_router = std::make_unique<NiceMock<MockMessageRouter>>();

        // Save raw pointers before ownership is transferred to sut_
        hal_wifi_ = hal_wifi.get();
        hal_timer_ = hal_timer.get();
        hal_freertos_ = hal_freertos.get();
        bootstrapper_ = bootstrapper.get();
        peer_mgr_ = peer_mgr.get();
        codec_ = codec.get();
        scanner_ = scanner.get();
        tx_fsm_ = tx_fsm.get();
        tx_mgr_ = tx_mgr.get();
        heartbeat_mgr_ = heartbeat_mgr.get();
        pairing_mgr_ = pairing_mgr.get();
        message_router_ = message_router.get();

        // -------------------------------------------------------------------
        // Default bootstrapper behaviour
        //
        // bootstrapper::init() populates the handles passed by reference —
        // without this the SUT would operate with null handles internally,
        // causing guards like (rx_dispatch_task_handle_ != nullptr) to fail.
        //
        // bootstrapper::deinit() clears them — mirrors real behaviour so
        // deinit() guards work correctly across multiple init/deinit cycles.
        // -------------------------------------------------------------------
        ON_CALL(*bootstrapper_, init(_, _, _, _, _, _, _))
            .WillByDefault(DoAll(
                SetArgReferee<2>(fake_queue),
                SetArgReferee<3>(fake_worker),
                SetArgReferee<4>(fake_mutex),
                SetArgReferee<5>(fake_rx_task),
                SetArgReferee<6>(fake_worker_task),
                Return(ESP_OK)));

        ON_CALL(*bootstrapper_, deinit(_, _, _, _, _))
            .WillByDefault(DoAll(
                SetArgReferee<0>(nullptr),
                SetArgReferee<1>(nullptr),
                SetArgReferee<2>(nullptr),
                SetArgReferee<3>(nullptr),
                SetArgReferee<4>(nullptr),
                Return(ESP_OK)));

        // peer_mgr: empty list by default — node starts in PAIRING state
        ON_CALL(*peer_mgr_, load_from_storage(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(etl::vector<PeerInfo, MAX_PEERS>{}));

        // submódule inits succeed by default
        ON_CALL(*tx_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*tx_mgr_, get_task_handle()).WillByDefault(Return(fake_worker_task));
        ON_CALL(*heartbeat_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*pairing_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*scanner_, init(_, _, _, _)).WillByDefault(Return(ESP_OK));

        // hal_freertos: semaphore used by confirm_reception ack_mutex_
        ON_CALL(*hal_freertos_, semaphore_take(_, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(*hal_freertos_, semaphore_give(_)).WillByDefault(Return(pdTRUE));

        sut_ = std::make_unique<EspNowManager>(
            std::move(hal_wifi),
            std::move(hal_timer),
            std::move(hal_freertos),
            std::move(bootstrapper),
            std::move(peer_mgr),
            std::move(codec),
            std::move(scanner),
            std::move(tx_fsm),
            std::move(tx_mgr),
            std::move(heartbeat_mgr),
            std::move(pairing_mgr),
            std::move(message_router));
    }

    // -----------------------------------------------------------------------
    // Helper: initialize sut_ with a valid config, all mocks succeeding.
    // -----------------------------------------------------------------------
    void init_sut() { ASSERT_EQ(sut_->init(make_valid_config()), ESP_OK); }
};

// ===========================================================================
// init() — guard conditions
// ===========================================================================

TEST_F(EspNowManagerTest, NodeStateIsUninitializedBeforeInit)
{
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
    EXPECT_FALSE(sut_->is_initialized());
}

TEST_F(EspNowManagerTest, InitReturnsInvalidArgIfAppQueueIsNull)
{
    EspNowConfig cfg = make_valid_config();
    cfg.app_rx_queue = nullptr;
    EXPECT_EQ(sut_->init(cfg), ESP_ERR_INVALID_ARG);
}

TEST_F(EspNowManagerTest, InitReturnsInvalidStateIfAlreadyInitialized)
{
    init_sut();
    EXPECT_EQ(sut_->init(make_valid_config()), ESP_ERR_INVALID_STATE);
}

TEST_F(EspNowManagerTest, NodeStateRemainsUninitializedAfterInvalidArgFailure)
{
    EspNowConfig cfg = make_valid_config();
    cfg.app_rx_queue = nullptr;
    sut_->init(cfg);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

// ===========================================================================
// init() — NodeState transitions
// ===========================================================================

TEST_F(EspNowManagerTest, InitWithNoPeersTransitionsToPairing)
{
    // peer_mgr returns empty list — node has no peers, must pair
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(etl::vector<PeerInfo, MAX_PEERS>{}));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

TEST_F(EspNowManagerTest, InitWithPeersTransitionsToOperational)
{
    // peer_mgr returns one peer — node is already associated
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = ReservedIds::HUB;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
}

TEST_F(EspNowManagerTest, IsInitializedReturnsTrueAfterSuccessfulInit)
{
    init_sut();
    EXPECT_TRUE(sut_->is_initialized());
}

// ===========================================================================
// init() — submódule delegation
// ===========================================================================

TEST_F(EspNowManagerTest, InitCallsBootstrapperInit)
{
    EXPECT_CALL(*bootstrapper_, init(_, _, _, _, _, _, _))
        .WillOnce(DoAll(
            SetArgReferee<2>(fake_queue),
            SetArgReferee<3>(fake_worker),
            SetArgReferee<4>(fake_mutex),
            SetArgReferee<5>(fake_rx_task),
            SetArgReferee<6>(fake_worker_task),
            Return(ESP_OK)));

    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfBootstrapperInitFails)
{
    ON_CALL(*bootstrapper_, init(_, _, _, _, _, _, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsTxManagerInit)
{
    EXPECT_CALL(*tx_mgr_, init(_, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfTxManagerInitFails)
{
    ON_CALL(*tx_mgr_, init(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsDiscoveryManagerInit)
{
    EXPECT_CALL(*scanner_, init(_, _, _, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitCallsDiscoveryManagerInitWhenNodeIsHub)
{
    EspNowConfig cfg = make_valid_config();
    cfg.node_type = kHubType;
    EXPECT_CALL(*scanner_, init(_, _, _, _)).WillOnce(Return(ESP_OK));
    sut_->init(cfg);
}

TEST_F(EspNowManagerTest, InitReturnsFailIfDiscoveryManagerInitFails)
{
    ON_CALL(*scanner_, init(_, _, _, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsHeartbeatManagerInit)
{
    EXPECT_CALL(*heartbeat_mgr_, init(_, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfHeartbeatManagerInitFails)
{
    ON_CALL(*heartbeat_mgr_, init(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsPairingManagerInit)
{
    EXPECT_CALL(*pairing_mgr_, init(_, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfPairingManagerInitFails)
{
    ON_CALL(*pairing_mgr_, init(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

// ===========================================================================
// init() — correct argument propagation to submódules
//
// These tests guard against swapped id/type arguments, which compile
// silently because both NodeId and NodeType are uint8_t aliases.
// ===========================================================================

TEST_F(EspNowManagerTest, InitPropagatesCorrectNodeIdAndTypeToPairingManager)
{
    // If id and type are swapped in EspNowManager::init(), this test fails
    EXPECT_CALL(*pairing_mgr_, init(kNodeId, kNodeType)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitPropagatesCorrectIntervalAndTypeToHeartbeatManager)
{
    EspNowConfig cfg = make_valid_config();
    cfg.heartbeat_interval_ms = 5000;

    // heartbeat_manager::init(interval_ms, node_type) — order matters
    EXPECT_CALL(*heartbeat_mgr_, init(cfg.heartbeat_interval_ms, kNodeType)).WillOnce(Return(ESP_OK));
    sut_->init(cfg);
}

TEST_F(EspNowManagerTest, InitCallsHeartbeatManagerUpdateNodeId)
{
    // update_node_id() is called after init() succeeds — must carry kNodeId
    EXPECT_CALL(*heartbeat_mgr_, update_node_id(kNodeId)).Times(1);
    sut_->init(make_valid_config());
}

// ===========================================================================
// deinit()
// ===========================================================================

TEST_F(EspNowManagerTest, DeinitIsIdempotentWhenNotInitialized)
{
    EXPECT_EQ(sut_->deinit(), ESP_OK);
    EXPECT_EQ(sut_->deinit(), ESP_OK);
}

TEST_F(EspNowManagerTest, DeinitTransitionsToUninitialized)
{
    init_sut();
    EXPECT_TRUE(sut_->is_initialized());
    sut_->deinit();
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
    EXPECT_FALSE(sut_->is_initialized());
}

TEST_F(EspNowManagerTest, ReinitAfterDeinitSucceeds)
{
    init_sut();
    sut_->deinit();
    EXPECT_EQ(sut_->init(make_valid_config()), ESP_OK);
}

TEST_F(EspNowManagerTest, DeinitCallsTxManagerDeinit)
{
    init_sut();
    EXPECT_CALL(*tx_mgr_, deinit()).WillOnce(Return(ESP_OK));
    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitCallsHeartbeatManagerDeinit)
{
    init_sut();
    EXPECT_CALL(*heartbeat_mgr_, deinit()).WillOnce(Return(ESP_OK));
    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitCallsBootstrapperDeinit)
{
    init_sut();
    EXPECT_CALL(*bootstrapper_, deinit(_, _, _, _, _))
        .WillOnce(DoAll(
            SetArgReferee<0>(nullptr),
            SetArgReferee<1>(nullptr),
            SetArgReferee<2>(nullptr),
            SetArgReferee<3>(nullptr),
            SetArgReferee<4>(nullptr),
            Return(ESP_OK)));
    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitWithPeersCallsDeletePeers)
{
    // peer_mgr returns one peer — node is already associated
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = ReservedIds::HUB;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    // esp_now_del_peer is called if peer list is not empty
    EXPECT_CALL(*hal_wifi_, hal_esp_now_del_peer(_)).WillOnce(Return(ESP_OK));
    sut_->deinit();
}

// ===========================================================================
// send_data()
// ===========================================================================

TEST_F(EspNowManagerTest, SendDataNonOperationalStateReturnsInvalidState)
{
    // Node is not initialized, NodeState::UNINITIALIZED
    EXPECT_EQ(sut_->send_data(kNodeId, kPayloadType, nullptr, false), ESP_ERR_INVALID_STATE);
}

TEST_F(EspNowManagerTest, SendDataToNonExistentPeerReturnsNotFound)
{
    // We need to insert a peer in the peer list to make the node operational
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(false)); // If a peer is not found, find_mac returns false
    EXPECT_EQ(sut_->send_data(kNodeId, kPayloadType, nullptr, false), ESP_ERR_NOT_FOUND);
}

TEST_F(EspNowManagerTest, IfEncodeReturnsZeroPacketSizeSendDataReturnsInvalidArg)
{
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(true));  // Peer is found, find_mac returns true
    ON_CALL(*codec_, encode(_, _, _, _, _)).WillByDefault(Return(0)); // If encode returns 0
    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, nullptr, false), ESP_ERR_INVALID_ARG);
}

TEST_F(EspNowManagerTest, SendDataToExistentPeerCallsQueuePacket)
{
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    ON_CALL(*codec_, encode(_, _, _, _, _)).WillByDefault(Return(10)); // Return non zero packet size
    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(true));   // Peer is found, find_mac returns true

    EXPECT_CALL(*tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK)); // Queue packet
    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, nullptr, 0), ESP_OK);
}

// ===========================================================================
// send_command()
// ===========================================================================

TEST_F(EspNowManagerTest, SendCommandNonOperationalStateReturnsInvalidState)
{
    // Node is not initialized, NodeState::UNINITIALIZED
    EXPECT_EQ(sut_->send_command(kNodeId, kCommandType, nullptr, false), ESP_ERR_INVALID_STATE);
}

TEST_F(EspNowManagerTest, SendCommandToNonExistentPeerReturnsNotFound)
{
    // We need to insert a peer in the peer list to make the node operational
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(false)); // If a peer is not found, find_mac returns false
    EXPECT_EQ(sut_->send_command(kNodeId, kCommandType, nullptr, false), ESP_ERR_NOT_FOUND);
}

TEST_F(EspNowManagerTest, IfEncodeReturnsZeroPacketSizeSendCommandReturnsInvalidArg)
{
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(true));  // Peer is found, find_mac returns true
    ON_CALL(*codec_, encode(_, _, _, _, _)).WillByDefault(Return(0)); // If encode returns 0
    EXPECT_EQ(sut_->send_command(kHubId, kCommandType, nullptr, false), ESP_ERR_INVALID_ARG);
}

TEST_F(EspNowManagerTest, SendCommandToExistentPeerCallsQueuePacket)
{
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    ON_CALL(*codec_, encode(_, _, _, _, _)).WillByDefault(Return(10)); // Return non zero packet size
    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(true));   // Peer is found, find_mac returns true

    EXPECT_CALL(*tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK)); // Queue packet
    EXPECT_EQ(sut_->send_command(kHubId, kCommandType, nullptr, 0), ESP_OK);
}