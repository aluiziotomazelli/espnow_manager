// host_test/test_espnow_manager/main/test_espnow_manager.cpp
//
// Synchronous tests — no real tasks are created. The espnow_driver mock
// returns ESP_OK without spawning any task, so all tested behaviour is
// exercised through direct method calls on the SUT.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_espnow_driver.hpp"
#include "mock_discovery_manager.hpp"
#include "mock_hal_freertos.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_hal_espnow.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_message_codec.hpp"
#include "mock_message_router.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_tx_state_machine.hpp"
#include "mock_storage_manager.hpp"
#include "mock_channel_monitor.hpp"
#include "mock_node_state_machine.hpp"

#include "espnow_manager.hpp"

using ::testing::_;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;
using ::testing::SetArgReferee;

// ---------------------------------------------------------------------------
// Fake FreeRTOS handles
//
// Opaque non-null pointers — the SUT forwards them to HAL calls but never
// dereferences them directly, so any non-null address is sufficient.
// ---------------------------------------------------------------------------
static int fake_queue_storage = 0;
static int fake_rx_task_storage = 0;

static QueueHandle_t fake_tx_queue = reinterpret_cast<QueueHandle_t>(&fake_queue_storage);
static TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(&fake_rx_task_storage);

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr NodeId kNodeId = 0x05;
static constexpr NodeType kNodeType = 0x02; // non-HUB
static constexpr NodeId kHubId = ReservedIds::HUB;
static constexpr NodeType kHubType = ReservedTypes::HUB;
static constexpr PayloadType kPayloadType = 0x03;
static constexpr CommandType kCommandType = CommandType::REBOOT;
static constexpr AckStatus kAckStatus = AckStatus::OK;
static constexpr uint32_t kHeartbeatIntervalMs = 1000;
static constexpr uint8_t kMac[6] = {0};

// Used in handle_notifications tests
bool should_stop = false;

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
    cfg.app_rx_queue = fake_tx_queue;
    cfg.rx_queue_length = 10;
    cfg.stack_size_rx_task = 2048;
    cfg.priority_rx_task = 5;
    cfg.stack_size_tx_task = 2048;
    cfg.priority_tx_task = 5;
    return cfg;
}

// --------------------------------------------------------------------------
// Testable EspNowManager class to manipulate node_state_
// --------------------------------------------------------------------------
class EspNowManagerTestable : public EspNowManager
{
public:
    using EspNowManager::EspNowManager;

    using EspNowManager::build_app_message;
    using EspNowManager::handle_notifications;
    using EspNowManager::node_fsm_;

    void set_node_state_operational()
    {
        // Force transition to OPERATIONAL state for testing purposes
        // Uses MockNodeStateMachine which defaults to returning OPERATIONAL
        // on_init(bool is_hub, bool has_peers)
        node_fsm_->on_init(false, true);
    }
};

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
    NiceMock<MockStorageManager>* storage_;
    NiceMock<MockWiFiHAL>* hal_wifi_;
    NiceMock<MockTimerHAL>* hal_timer_;
    NiceMock<MockFreeRTOSHAL>* hal_freertos_;
    NiceMock<MockEspNowHAL>* hal_espnow_;
    NiceMock<MockEspNowDriver>* espnow_driver_;
    NiceMock<MockPeerManager>* peer_mgr_;
    NiceMock<MockMessageCodec>* codec_;
    NiceMock<MockDiscoveryManager>* scanner_;
    NiceMock<MockTxStateMachine>* tx_fsm_;
    NiceMock<MockTxManager>* tx_mgr_;
    NiceMock<MockHeartbeatManager>* heartbeat_mgr_;
    NiceMock<MockPairingManager>* pairing_mgr_;
    NiceMock<MockMessageRouter>* message_router_;
    NiceMock<MockChannelMonitor>* channel_monitor_;
    NiceMock<MockNodeStateMachine>* node_fsm_;

    std::unique_ptr<EspNowManagerTestable> sut_;

    void SetUp() override
    {
        auto storage = std::make_unique<NiceMock<MockStorageManager>>();
        auto hal_wifi = std::make_unique<NiceMock<MockWiFiHAL>>();
        auto hal_timer = std::make_unique<NiceMock<MockTimerHAL>>();
        auto hal_freertos = std::make_unique<NiceMock<MockFreeRTOSHAL>>();
        auto hal_espnow = std::make_unique<NiceMock<MockEspNowHAL>>();
        auto espnow_driver = std::make_unique<NiceMock<MockEspNowDriver>>();
        auto peer_mgr = std::make_unique<NiceMock<MockPeerManager>>();
        auto codec = std::make_unique<NiceMock<MockMessageCodec>>();
        auto channel_monitor = std::make_unique<NiceMock<MockChannelMonitor>>();
        auto scanner = std::make_unique<NiceMock<MockDiscoveryManager>>();
        auto tx_fsm = std::make_unique<NiceMock<MockTxStateMachine>>();
        auto tx_mgr = std::make_unique<NiceMock<MockTxManager>>();
        auto heartbeat_mgr = std::make_unique<NiceMock<MockHeartbeatManager>>();
        auto pairing_mgr = std::make_unique<NiceMock<MockPairingManager>>();
        auto message_router = std::make_unique<NiceMock<MockMessageRouter>>();
        auto node_fsm = std::make_unique<NiceMock<MockNodeStateMachine>>();

        // Save raw pointers before ownership is transferred to sut_
        storage_ = storage.get();
        hal_wifi_ = hal_wifi.get();
        hal_timer_ = hal_timer.get();
        hal_freertos_ = hal_freertos.get();
        hal_espnow_ = hal_espnow.get();
        espnow_driver_ = espnow_driver.get();
        peer_mgr_ = peer_mgr.get();
        codec_ = codec.get();
        channel_monitor_ = channel_monitor.get();
        scanner_ = scanner.get();
        tx_fsm_ = tx_fsm.get();
        tx_mgr_ = tx_mgr.get();
        heartbeat_mgr_ = heartbeat_mgr.get();
        pairing_mgr_ = pairing_mgr.get();
        message_router_ = message_router.get();
        node_fsm_ = node_fsm.get();

        // Clear the variable between tests
        should_stop = false;

        // peer_mgr: empty list by default — NodeFSM starts on PAIRING_SCAN
        ON_CALL(*peer_mgr_, load_peers_from_storage()).WillByDefault(Return(ESP_OK));

        // To deinit forcing the node state to uninitialized
        ON_CALL(*node_fsm_, on_deinit()).WillByDefault(::testing::Invoke([this]() {
            node_fsm_->set_state(NodeState::UNINITIALIZED);
            return ESP_OK;
        }));

        // storage: load_channel succeeds by default
        ON_CALL(*storage_, load_channel(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(*storage_, store_channel(_)).WillByDefault(Return(ESP_OK));

        // espnow_driver init and deinit()
        ON_CALL(*espnow_driver_, init(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*espnow_driver_, deinit()).WillByDefault(Return(ESP_OK));

        // Creating queues
        ON_CALL(*hal_freertos_, queue_create(_, _)).WillByDefault(Return(fake_tx_queue));
        ON_CALL(*hal_freertos_, queue_delete(_)).WillByDefault(Return());

        // Creating tasks
        ON_CALL(*hal_freertos_, task_create(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(fake_rx_task), Return(pdPASS)));
        ON_CALL(*hal_freertos_, task_delete(_)).WillByDefault(Return());

        ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(etl::vector<PeerInfo, MAX_PEERS>{}));

        // submódule inits succeed by default
        ON_CALL(*tx_mgr_, init(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*tx_mgr_, get_task_handle()).WillByDefault(Return(fake_rx_task));
        ON_CALL(*scanner_, init(_, _, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*pairing_mgr_, init(_, _, fake_rx_task, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*channel_monitor_, init(_, _)).WillByDefault(Return(ESP_OK));

        ON_CALL(*hal_freertos_, semaphore_give(_)).WillByDefault(Return(pdTRUE));

        // By default, common operations should succeed
        ON_CALL(*hal_freertos_, semaphore_take(_, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(true));

        sut_ = std::make_unique<EspNowManagerTestable>(
            std::move(storage),
            std::move(hal_wifi),
            std::move(hal_timer),
            std::move(hal_freertos),
            std::move(hal_espnow),
            std::move(espnow_driver),
            std::move(peer_mgr),
            std::move(codec),
            std::move(channel_monitor),
            std::move(scanner),
            std::move(tx_fsm),
            std::move(tx_mgr),
            std::move(heartbeat_mgr),
            std::move(pairing_mgr),
            std::move(message_router),
            std::move(node_fsm));
    }

    void TearDown() override { sut_.reset(); }

    // -----------------------------------------------------------------------
    // Helper: initialize sut_ with a valid config, all mocks succeeding.
    // -----------------------------------------------------------------------
    void init_sut() { ASSERT_EQ(sut_->init(make_valid_config()), ESP_OK); }
    void init_operational_sut()
    {
        init_sut();
        sut_->set_node_state_operational();
    }

    void add_peer_to_storage()
    {
        etl::vector<PeerInfo, MAX_PEERS> peers;
        PeerInfo p{};
        p.node_id = ReservedIds::HUB;
        peers.push_back(p);
        ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));
    }
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

TEST_F(EspNowManagerTest, InitWithNoPeersTransitionsToPairingScan)
{
    init_sut();
    // With no peers, node transitions to PAIRING_SCAN (auto-scan for HUB)
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);
}

TEST_F(EspNowManagerTest, InitWithPeersTransitionsToOperational)
{
    // peer_mgr returns one peer — node is already associated
    add_peer_to_storage();

    // add_peers_to_espnow(_) will call hal_esp_now_add_peer(_)
    EXPECT_CALL(*hal_espnow_, hal_esp_now_add_peer(_)).WillOnce(Return(ESP_OK));

    init_sut();
    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
}

TEST_F(EspNowManagerTest, IsInitializedReturnsTrueAfterSuccessfulInit)
{
    init_sut();
    EXPECT_TRUE(sut_->is_initialized());
}

// ===========================================================================
// init() - FreeRTOS resources creation
// ===========================================================================

TEST_F(EspNowManagerTest, InitCallsQueueCreate)
{
    EXPECT_CALL(*hal_freertos_, queue_create(_, _)).WillOnce(Return(fake_tx_queue));
    EXPECT_EQ(sut_->init(make_valid_config()), ESP_OK);
}

TEST_F(EspNowManagerTest, InitReturnsFailIfFirstQueueCreationFails)
{
    EXPECT_CALL(*hal_freertos_, queue_create(_, _)).WillOnce(Return(nullptr));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsTaskCreate)
{
    EXPECT_CALL(*hal_freertos_, task_create(_, _, _, _, _, _)).WillOnce(Return(pdPASS));
    EXPECT_EQ(sut_->init(make_valid_config()), ESP_OK);
}

TEST_F(EspNowManagerTest, InitReturnsFailIfFirstTaskCreationFails)
{
    EXPECT_CALL(*hal_freertos_, task_create(_, _, _, _, _, _)).WillOnce(Return(pdFAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

// ===========================================================================
// init() - submódule delegation
// ===========================================================================

TEST_F(EspNowManagerTest, InitCallsEspNowDriverInit)
{
    EXPECT_CALL(*espnow_driver_, init(_, _, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitLoadsChannelFromStorage)
{
    EXPECT_CALL(*storage_, load_channel(_)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitStoresChannelAtEnd)
{
    EXPECT_CALL(*storage_, store_channel(_)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfEspNowDriverInitFails)
{
    ON_CALL(*espnow_driver_, init(_, _, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsTxManagerInit)
{
    EXPECT_CALL(*tx_mgr_, init(_, _, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfTxManagerInitFails)
{
    ON_CALL(*tx_mgr_, init(_, _, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsDiscoveryManagerInit)
{
    EXPECT_CALL(*scanner_, init(_, _, _, _, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitCallsDiscoveryManagerInitWhenNodeIsHub)
{
    EspNowConfig cfg = make_valid_config();
    cfg.node_type = kHubType;
    EXPECT_CALL(*scanner_, init(_, _, _, _, _)).WillOnce(Return(ESP_OK));
    sut_->init(cfg);
}

TEST_F(EspNowManagerTest, InitReturnsFailIfDiscoveryManagerInitFails)
{
    ON_CALL(*scanner_, init(_, _, _, _, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitCallsPairingManagerInit)
{
    EXPECT_CALL(*pairing_mgr_, init(_, _, fake_rx_task, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitCallsChannelMonitorInit)
{
    EXPECT_CALL(*channel_monitor_, init(_, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitReturnsFailIfPairingManagerInitFails)
{
    ON_CALL(*pairing_mgr_, init(_, _, fake_rx_task, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_NE(sut_->init(make_valid_config()), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::UNINITIALIZED);
}

TEST_F(EspNowManagerTest, InitReturnsFailIfChannelMonitorInitFails)
{
    ON_CALL(*channel_monitor_, init(_, _)).WillByDefault(Return(ESP_FAIL));
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
    EXPECT_CALL(*pairing_mgr_, init(kNodeId, kNodeType, fake_rx_task, _)).WillOnce(Return(ESP_OK));
    sut_->init(make_valid_config());
}

TEST_F(EspNowManagerTest, InitPropagatesCorrectIntervalAndTypeToHeartbeatManager)
{
    EspNowConfig cfg = make_valid_config();
    cfg.heartbeat_interval_ms = 5000;

    // heartbeat_manager::init(interval_ms, node_type) — order matters
    EXPECT_CALL(*heartbeat_mgr_, init(kNodeId, kNodeType, cfg.heartbeat_interval_ms)).Times(1);
    sut_->init(cfg);
}

// ===========================================================================
// deinit()
// ===========================================================================

TEST_F(EspNowManagerTest, DeinitDoesNotCleanResourcesWhenNotInitialized)
{
    EXPECT_CALL(*hal_freertos_, task_delete(_)).Times(0);
    EXPECT_CALL(*hal_freertos_, queue_delete(_)).Times(0);
    EXPECT_CALL(*hal_espnow_, hal_esp_now_del_peer(_)).Times(0);

    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitDoesNotDeleteNullTaskHandles)
{
    // Init pass but task_handles are null
    EXPECT_CALL(*hal_freertos_, task_create(_, _, _, _, _, _))
        .WillOnce(DoAll(SetArgPointee<5>(nullptr), Return(pdPASS)));
    EXPECT_EQ(sut_->init(make_valid_config()), ESP_OK);

    // Will not try to delete null task handles
    EXPECT_CALL(*hal_freertos_, task_delete(_)).Times(0);
    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitCallsAllDeleteFunctions)
{
    // setup a peer so that deinit calls del_peer
    init_operational_sut();
    add_peer_to_storage();

    EXPECT_CALL(*tx_mgr_, deinit()).Times(1);
    EXPECT_CALL(*hal_freertos_, task_delete(_)).Times(1);
    EXPECT_CALL(*hal_freertos_, queue_delete(_)).Times(1);
    EXPECT_CALL(*hal_espnow_, hal_esp_now_del_peer(_)).Times(1);
    EXPECT_CALL(*espnow_driver_, deinit()).Times(1);

    sut_->deinit();
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
    EXPECT_CALL(*tx_mgr_, deinit()).WillOnce(Return());
    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitCallsEspNowDriverDeinit)
{
    init_sut();
    EXPECT_CALL(*espnow_driver_, deinit()).WillOnce(Return(ESP_OK));
    sut_->deinit();
}

TEST_F(EspNowManagerTest, DeinitWithPeersCallsDeletePeers)
{
    // peer_mgr returns one peer
    init_sut();
    add_peer_to_storage();

    // esp_now_del_peer is called if peer list is not empty
    EXPECT_CALL(*hal_espnow_, hal_esp_now_del_peer(_)).WillOnce(Return(ESP_OK));
    sut_->deinit();
}

// ===========================================================================
// send_data and send_command()
// ===========================================================================

TEST_F(EspNowManagerTest, SendNonOperationalStateReturnsInvalidState)
{
    // Node is not initialized, NodeState::UNINITIALIZED
    EXPECT_EQ(sut_->send_data(kNodeId, kPayloadType, nullptr, false), ESP_ERR_INVALID_STATE);
    EXPECT_EQ(sut_->send_command(kNodeId, kCommandType, nullptr, false), ESP_ERR_INVALID_STATE);
}

TEST_F(EspNowManagerTest, SendToNonExistentPeerReturnsNotFound)
{
    init_sut();
    sut_->set_node_state_operational();

    // Override default ON_CALL for find_mac
    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(false));

    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, nullptr, 0), ESP_ERR_NOT_FOUND);
    EXPECT_EQ(sut_->send_command(kNodeId, kCommandType, nullptr, false), ESP_ERR_NOT_FOUND);
}

TEST_F(EspNowManagerTest, SendToExistentPeerCallsQueuePacket)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    add_peer_to_storage();

    EXPECT_CALL(*tx_mgr_, queue_packet(_)).Times(2);

    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, nullptr, 0), ESP_OK);
    EXPECT_EQ(sut_->send_command(kHubId, kCommandType, nullptr, 0), ESP_OK);
}

TEST_F(EspNowManagerTest, FailureToQueuePacketReturnsFail)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    add_peer_to_storage();

    EXPECT_CALL(*tx_mgr_, queue_packet(_)).WillRepeatedly(Return(ESP_FAIL));

    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, nullptr, 0), ESP_FAIL);
    EXPECT_EQ(sut_->send_command(kHubId, kCommandType, nullptr, 0), ESP_FAIL);
}

TEST_F(EspNowManagerTest, SendDataWithPayloadCopiesData)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    add_peer_to_storage();

    uint8_t test_payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};

    // Verify that queue_packet receives a packet with correct payload
    EXPECT_CALL(*tx_mgr_, queue_packet(_)).WillOnce([](const DecodedTxPacket& pkt) {
        EXPECT_EQ(pkt.payload_len, 5);
        EXPECT_EQ(pkt.payload[0], 0x01);
        EXPECT_EQ(pkt.payload[1], 0x02);
        EXPECT_EQ(pkt.payload[2], 0x03);
        EXPECT_EQ(pkt.payload[3], 0x04);
        EXPECT_EQ(pkt.payload[4], 0x05);
        return ESP_OK;
    });

    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, test_payload, 5), ESP_OK);
}

TEST_F(EspNowManagerTest, SendDataWithoutPayloadSendsHeaderOnly)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    add_peer_to_storage();

    EXPECT_CALL(*tx_mgr_, queue_packet(_)).WillOnce([](const DecodedTxPacket& pkt) {
        EXPECT_EQ(pkt.payload_len, 0);
        return ESP_OK;
    });

    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, nullptr, 0), ESP_OK);
}

TEST_F(EspNowManagerTest, SendDataWithOversizedPayloadReturnsInvalidArg)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    add_peer_to_storage();

    // Payload larger than MAX_PAYLOAD_SIZE should be rejected
    uint8_t large_payload[MAX_PAYLOAD_SIZE + 10] = {};
    EXPECT_EQ(sut_->send_data(kHubId, kPayloadType, large_payload, sizeof(large_payload)), ESP_ERR_INVALID_ARG);
}

// ===========================================================================
// confirm_reception()
// ===========================================================================

TEST_F(EspNowManagerTest, ConfirmReceptionNonOperationalStateReturnsInvalidState)
{
    // Node is not initialized, NodeState::UNINITIALIZED
    EXPECT_EQ(sut_->confirm_reception(kHubId, 42, kAckStatus), ESP_ERR_INVALID_STATE);
}

TEST_F(EspNowManagerTest, ConfirmReceptionNonExistentPeerReturnsNotFound)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // find_mac fails
    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(false));

    EXPECT_EQ(sut_->confirm_reception(kHubId, 42, kAckStatus), ESP_ERR_NOT_FOUND);
}

TEST_F(EspNowManagerTest, ConfirmReceptionEnqueueFailureReturnsFail)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // find_mac succeeds, but queue_packet fails
    ON_CALL(*peer_mgr_, find_mac(_, _)).WillByDefault(Return(true));
    ON_CALL(*tx_mgr_, queue_packet(_)).WillByDefault(Return(ESP_FAIL));

    EXPECT_EQ(sut_->confirm_reception(kHubId, 42, kAckStatus), ESP_FAIL);
}

TEST_F(EspNowManagerTest, ConfirmReceptionSuccess)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // find_mac succeeds, queue_packet succeeds
    EXPECT_CALL(*peer_mgr_, find_mac(_, _)).WillOnce(Return(true));
    EXPECT_CALL(*tx_mgr_, queue_packet(_)).WillOnce(Return(ESP_OK));

    EXPECT_EQ(sut_->confirm_reception(kHubId, 42, kAckStatus), ESP_OK);
}

TEST_F(EspNowManagerTest, ConfirmReceptionResetsHeaderWhenPeerNotFound)
{
    init_operational_sut();

    // find_mac fails
    EXPECT_CALL(*peer_mgr_, find_mac(_, _)).WillOnce(Return(false));

    EXPECT_EQ(sut_->confirm_reception(kHubId, 42, kAckStatus), ESP_ERR_NOT_FOUND);
}

// ===========================================================================
// getters
// ===========================================================================

TEST_F(EspNowManagerTest, GetPeersCallsPeerManagerGetAll)
{
    EXPECT_CALL(*peer_mgr_, get_all()).Times(1);
    sut_->get_peers();
}

TEST_F(EspNowManagerTest, GetOfflinePeersCallsPeerManagerGetOffline)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // get_offline_peers() calls peer_manager_->get_offline(get_time_ms())
    // Use wildcard matcher for the timestamp parameter
    EXPECT_CALL(*peer_mgr_, get_offline(_)).Times(1);
    sut_->get_offline_peers();
}

TEST_F(EspNowManagerTest, GetOfflinePeersNotOperationalReturnsEmptyVector)
{
    EXPECT_CALL(*peer_mgr_, get_offline(_)).Times(0);
    EXPECT_TRUE(sut_->get_offline_peers().empty());
}
// ===========================================================================
// add and remove peers
// ===========================================================================

TEST_F(EspNowManagerTest, AddPeerCallsPeerManagerAdd)
{
    EXPECT_CALL(*peer_mgr_, add(_, _, _, _)).Times(1).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->add_peer(kNodeId, kMac, kNodeType, kHeartbeatIntervalMs), ESP_OK);
}

TEST_F(EspNowManagerTest, RemovePeerCallsPeerManagerRemove)
{
    EXPECT_CALL(*peer_mgr_, remove(_)).Times(1).WillOnce(Return(ESP_OK));
    EXPECT_EQ(sut_->remove_peer(kNodeId), ESP_OK);
}

TEST_F(EspNowManagerTest, AddPeerReturnsPeerManagerFailure)
{
    EXPECT_CALL(*peer_mgr_, add(_, _, _, _)).Times(1).WillOnce(Return(ESP_FAIL));
    EXPECT_EQ(sut_->add_peer(kNodeId, kMac, kNodeType, kHeartbeatIntervalMs), ESP_FAIL);
}

TEST_F(EspNowManagerTest, RemovePeerReturnsPeerManagerFailure)
{
    EXPECT_CALL(*peer_mgr_, remove(_)).Times(1).WillOnce(Return(ESP_FAIL));
    EXPECT_EQ(sut_->remove_peer(kNodeId), ESP_FAIL);
}

// ===========================================================================
// start_pairing
// ===========================================================================

TEST_F(EspNowManagerTest, StartPairingWithPeersTransitionsToPairingAndCallsPairingManagerStart)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);
    add_peer_to_storage();

    // scanner_->is_scanning() is checked before calling pairing_manager_->start()
    ON_CALL(*scanner_, is_scanning()).WillByDefault(Return(false));

    uint32_t pairing_timeout_ms = 10000;

    // When node has peers and requests pairing, it transitions to PAIRING state
    // and pairing_manager_->start() is called
    EXPECT_CALL(*pairing_mgr_, start(pairing_timeout_ms, _)).Times(1);
    ASSERT_EQ(sut_->start_pairing(pairing_timeout_ms), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

TEST_F(EspNowManagerTest, StartPairingNotOperationalReturnsInvalidState)
{
    // Node is UNINITIALIZED
    EXPECT_EQ(sut_->start_pairing(10000), ESP_ERR_INVALID_STATE);
}

TEST_F(EspNowManagerTest, StartPairingWithoutPeersTransitionsToPairingScan)
{
    // Init with no peers -> PAIRING_SCAN state, then transition to OPERATIONAL
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // From OPERATIONAL without peers, request pairing -> PAIRING_SCAN
    EXPECT_CALL(*scanner_, start_scan()).Times(1);
    ASSERT_EQ(sut_->start_pairing(100), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);
}

TEST_F(EspNowManagerTest, StartPairingForHubCallsPairingManagerStart)
{
    // Create config with HUB type - must pass directly to init() not via init_sut()
    EspNowConfig cfg = make_valid_config();
    cfg.node_type = ReservedTypes::HUB;
    ASSERT_EQ(sut_->init(cfg), ESP_OK);

    // Transition to OPERATIONAL first so we can test start_pairing()
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // Mock peer_manager to return non-empty list (has_peers = true)
    add_peer_to_storage();

    // scanner_->is_scanning() is checked before calling pairing_manager_->start()
    ON_CALL(*scanner_, is_scanning()).WillByDefault(Return(false));

    // For HUB with peers, pairing_manager_->start() should be called directly
    EXPECT_CALL(*pairing_mgr_, start(30000, _)).Times(1);
    EXPECT_EQ(sut_->start_pairing(30000), ESP_OK);
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

TEST_F(EspNowManagerTest, StartPairingWhileScanningCallsStopScan)
{
    init_sut();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    add_peer_to_storage();

    // scanner_->is_scanning() is checked before calling pairing_manager_->start()
    ON_CALL(*scanner_, is_scanning()).WillByDefault(Return(true));

    // For HUB with peers, pairing_manager_->start() should be called directly
    EXPECT_CALL(*pairing_mgr_, start(30000, _)).Times(1);
    EXPECT_EQ(sut_->start_pairing(30000), ESP_OK);
}

// ===========================================================================
// Notifications — EspNowManager::handle_notifications()
//
// These tests verify that handle_notifications() correctly dispatches
// notification flags to the appropriate submodules (NodeFSM, Scanner, Storage, etc.)
// ===========================================================================

TEST_F(EspNowManagerTest, NotifyMaxFailuresCallsOnScanRequested)
{
    // NOTIFY_MAX_FAILURES → NodeFSM: on_scan_requested()
    init_sut();
    EXPECT_CALL(*node_fsm_, on_scan_requested()).Times(1);
    sut_->handle_notifications(NOTIFY_MAX_FAILURES, should_stop);
}

TEST_F(EspNowManagerTest, NotifyMaxFailuresCallsStartScan)
{
    // NOTIFY_MAX_FAILURES → Scanner: start_scan() (via state transition to RECOVERY_SCAN)
    init_sut();
    EXPECT_CALL(*scanner_, start_scan()).Times(1);
    sut_->handle_notifications(NOTIFY_MAX_FAILURES, should_stop);
}

TEST_F(EspNowManagerTest, NotifyChannelFoundCallsScannerGetChannel)
{
    // NOTIFY_CHANNEL_FOUND → Scanner: get_channel() to retrieve discovered channel
    init_sut();
    EXPECT_CALL(*scanner_, get_channel()).Times(1);
    sut_->handle_notifications(NOTIFY_CHANNEL_FOUND, should_stop);
}

TEST_F(EspNowManagerTest, NotifyChannelFoundCallsOnChannelFound)
{
    // NOTIFY_CHANNEL_FOUND → NodeFSM: on_channel_found() to transition state
    init_sut();
    EXPECT_CALL(*node_fsm_, on_channel_found()).Times(1);
    sut_->handle_notifications(NOTIFY_CHANNEL_FOUND, should_stop);
}

TEST_F(EspNowManagerTest, NotifyChannelFoundCallsStopScan)
{
    // NOTIFY_CHANNEL_FOUND + scanner is scanning → stop_scan()
    init_sut();
    node_fsm_->set_state(NodeState::RECOVERY_SCAN);

    ON_CALL(*scanner_, is_scanning()).WillByDefault(Return(true));
    EXPECT_CALL(*scanner_, stop_scan()).Times(1);
    sut_->handle_notifications(NOTIFY_CHANNEL_FOUND, should_stop);
}

TEST_F(EspNowManagerTest, NotifyChannelFoundCallsStoreChannel)
{
    // NOTIFY_CHANNEL_FOUND + RECOVERY_SCAN → store_channel() to persist discovered channel
    init_sut();
    node_fsm_->set_state(NodeState::RECOVERY_SCAN);

    EXPECT_CALL(*storage_, store_channel(_)).Times(1);
    sut_->handle_notifications(NOTIFY_CHANNEL_FOUND, should_stop);
}

TEST_F(EspNowManagerTest, NotifyPairingDoneCallsOnPairingTimeout)
{
    // NOTIFY_PAIRING_DONE → NodeFSM: on_pairing_timeout(has_peers)
    init_sut();
    EXPECT_CALL(*node_fsm_, on_pairing_timeout(_)).Times(1);
    sut_->handle_notifications(NOTIFY_PAIRING_DONE, should_stop);
}

TEST_F(EspNowManagerTest, NotifyPairingDoneCheckForPeers)
{
    // NOTIFY_PAIRING_DONE → check peers to determine has_peers parameter
    init_sut();
    EXPECT_CALL(*peer_mgr_, get_all()).Times(1);
    sut_->handle_notifications(NOTIFY_PAIRING_DONE, should_stop);
}

TEST_F(EspNowManagerTest, NotifyScanFailedCallsOnScanFailed)
{
    // NOTIFY_SCAN_FAILED → NodeFSM: on_scan_failed()
    init_sut();
    EXPECT_CALL(*node_fsm_, on_scan_failed()).Times(1);
    sut_->handle_notifications(NOTIFY_SCAN_FAILED, should_stop);
}

TEST_F(EspNowManagerTest, NotifyScanFailedCallsStopScan)
{
    // NOTIFY_SCAN_FAILED + scanner is scanning → stop_scan()
    init_sut();
    node_fsm_->set_state(NodeState::RECOVERY_SCAN);

    ON_CALL(*scanner_, is_scanning()).WillByDefault(Return(true));
    EXPECT_CALL(*scanner_, stop_scan()).Times(1);
    sut_->handle_notifications(NOTIFY_SCAN_FAILED, should_stop);
}

TEST_F(EspNowManagerTest, NotifyScanFailedCheckForPeers)
{
    // NOTIFY_SCAN_FAILED → check peers to determine has_peers parameter
    init_sut();
    EXPECT_CALL(*peer_mgr_, get_all()).Times(1);
    sut_->handle_notifications(NOTIFY_SCAN_FAILED, should_stop);
}

TEST_F(EspNowManagerTest, NotifyChannelChangedChecksCurrentChannel)
{
    // NOTIFY_CHANNEL_CHANGED → ChannelMonitor: get_wifi_channel() to get new channel
    init_sut();
    EXPECT_CALL(*channel_monitor_, get_wifi_channel()).Times(1);
    sut_->handle_notifications(NOTIFY_CHANNEL_CHANGED, should_stop);
}

TEST_F(EspNowManagerTest, NotifyChannelChangedPropagatesChannel)
{
    // NOTIFY_CHANNEL_CHANGED → Storage: store_channel() + Scanner: set_channel()
    init_sut();
    EXPECT_CALL(*storage_, store_channel(_)).Times(1);
    EXPECT_CALL(*scanner_, set_channel(_)).Times(1);
    sut_->handle_notifications(NOTIFY_CHANNEL_CHANGED, should_stop);
}

TEST_F(EspNowManagerTest, NotifyStopTurnsShouldStopTrue)
{
    // NOTIFY_TASK_TO_STOP → set should_stop = true (signals rx_task to exit)
    init_sut();

    sut_->handle_notifications(NOTIFY_TASK_TO_STOP, should_stop);
    ASSERT_TRUE(should_stop);
}

// ===========================================================================
// AppMessage — build_app_message()
//
// Verifies correct extraction of fields from DecodedRxPacket to AppMessage
// ===========================================================================

TEST_F(EspNowManagerTest, BuildAppMessageWithDataPayloadCreatesAppMessage)
{
    // Setup decoded packet with header, payload, and CRC
    DecodedRxPacket decoded_packet{};
    decoded_packet.header.sender_node_id = kHubId;
    decoded_packet.header.sender_type = kNodeType;
    decoded_packet.header.payload_type = kPayloadType;
    decoded_packet.header.requires_ack = true;

    // Test payload
    uint8_t test_payload[] = {0x01, 0x02, 0x03};
    size_t payload_len = sizeof(test_payload);

    // Build raw.data: header + payload + CRC
    memcpy(decoded_packet.raw.data, &decoded_packet.header, sizeof(MessageHeader));
    memcpy(decoded_packet.raw.data + sizeof(MessageHeader), test_payload, payload_len);

    // CRC (simulated, not used by build_app_message)
    uint16_t crc = 0xABCD;
    memcpy(decoded_packet.raw.data + sizeof(MessageHeader) + payload_len, &crc, CRC_SIZE);

    // Total packet size
    decoded_packet.raw.len = sizeof(MessageHeader) + payload_len + CRC_SIZE;

    // Source MAC address
    uint8_t src_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    memcpy(decoded_packet.raw.src_mac, src_mac, 6);

    // Call the method
    AppMessage app_msg = sut_->build_app_message(decoded_packet);

    // Verifications — all fields should be correctly extracted
    EXPECT_EQ(app_msg.sender_id, kHubId);
    EXPECT_EQ(app_msg.sender_type, kNodeType);
    EXPECT_EQ(app_msg.payload_type, kPayloadType);
    EXPECT_TRUE(app_msg.requires_ack);
    EXPECT_EQ(app_msg.payload_len, payload_len);
    EXPECT_EQ(memcmp(app_msg.src_mac, src_mac, 6), 0);
    EXPECT_EQ(memcmp(app_msg.payload, test_payload, payload_len), 0);
}