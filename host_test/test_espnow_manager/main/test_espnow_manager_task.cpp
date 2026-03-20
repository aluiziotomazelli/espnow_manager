// host_test/test_espnow_manager/main/test_espnow_manager_task.cpp
//
// Task-based tests — real FreeRTOS tasks are created by EspNowManager::init()
// via the injected RealFreeRTOSHAL. All hardware-dependent components
// (WiFi, Timer, ESP-NOW driver) remain mocked since the host has no hardware.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_espnow_driver.hpp"
#include "mock_discovery_manager.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_message_codec.hpp"
#include "mock_message_router.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_tx_manager.hpp"
#include "mock_tx_state_machine.hpp"
#include "hal_real_freertos.hpp"

#include "espnow_manager.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr NodeId kNodeId = 0x05;
static constexpr NodeType kNodeType = 0x02; // non-HUB
static constexpr uint32_t kDelayMs = 20;    // time to let tasks process one iteration
static constexpr uint32_t kTickMs = 120;    // slightly longer than queue_receive timeout (100ms)

// Slightly longer than queue_receive timeout (100ms) to guarantee
// the rx_dispatch_task has completed at least one full loop iteration
// and processed any pending notifications.
// Must be > pdMS_TO_TICKS(100) — the queue_receive timeout in rx_dispatch_task
static constexpr uint32_t notify_delay_ms = 105;

// ---------------------------------------------------------------------------
// Testable subclass — exposes protected callbacks for direct test invocation
// ---------------------------------------------------------------------------
class EspNowManagerTestable : public EspNowManager
{
public:
    using EspNowManager::EspNowManager;

    void set_node_state(NodeState state) { node_state_.store(state); }

    // Expose IChannelObserver callbacks — normally called from TxManager task
    void on_channel_found_cb(uint8_t ch) override { EspNowManager::on_channel_found_cb(ch); }
    void on_scan_failed_cb() override { EspNowManager::on_scan_failed_cb(); }
    void on_scan_started_cb() override { EspNowManager::on_scan_started_cb(); }
};

// ---------------------------------------------------------------------------
// Fixture
//
// RealFreeRTOSHAL is injected so EspNowManager::init() creates real tasks,
// queues and mutex via create_tasks(), create_queues(), create_mutex().
// All other dependencies remain mocked.
//
// TearDown calls deinit() which signals tasks to stop and waits up to 1s
// for graceful exit before force-deleting them.
// ---------------------------------------------------------------------------
class EspNowManagerTaskTest : public ::testing::Test
{
protected:
    // Raw pointers for test access — owned by sut_
    NiceMock<MockEspNowDriver> *driver_;
    NiceMock<MockTimerHAL> *hal_timer_;
    NiceMock<MockWiFiHAL> *hal_wifi_;
    NiceMock<MockPeerManager> *peer_mgr_;
    NiceMock<MockMessageCodec> *codec_;
    NiceMock<MockDiscoveryManager> *scanner_;
    NiceMock<MockTxStateMachine> *tx_fsm_;
    NiceMock<MockTxManager> *tx_mgr_;
    NiceMock<MockHeartbeatManager> *heartbeat_mgr_;
    NiceMock<MockPairingManager> *pairing_mgr_;
    NiceMock<MockMessageRouter> *message_router_;

    std::unique_ptr<EspNowManagerTestable> sut_;

    void SetUp() override
    {
        auto driver = std::make_unique<NiceMock<MockEspNowDriver>>();
        auto hal_timer = std::make_unique<NiceMock<MockTimerHAL>>();
        auto hal_wifi = std::make_unique<NiceMock<MockWiFiHAL>>();
        auto peer_mgr = std::make_unique<NiceMock<MockPeerManager>>();
        auto codec = std::make_unique<NiceMock<MockMessageCodec>>();
        auto scanner = std::make_unique<NiceMock<MockDiscoveryManager>>();
        auto tx_fsm = std::make_unique<NiceMock<MockTxStateMachine>>();
        auto tx_mgr = std::make_unique<NiceMock<MockTxManager>>();
        auto heartbeat_mgr = std::make_unique<NiceMock<MockHeartbeatManager>>();
        auto pairing_mgr = std::make_unique<NiceMock<MockPairingManager>>();
        auto message_router = std::make_unique<NiceMock<MockMessageRouter>>();

        driver_ = driver.get();
        hal_timer_ = hal_timer.get();
        hal_wifi_ = hal_wifi.get();
        peer_mgr_ = peer_mgr.get();
        codec_ = codec.get();
        scanner_ = scanner.get();
        tx_fsm_ = tx_fsm.get();
        tx_mgr_ = tx_mgr.get();
        heartbeat_mgr_ = heartbeat_mgr.get();
        pairing_mgr_ = pairing_mgr.get();
        message_router_ = message_router.get();

        // EspNowDriver succeeds by default — no real WiFi needed on host
        ON_CALL(*driver_, init(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*driver_, deinit()).WillByDefault(Return(ESP_OK));

        // peer_mgr: empty list by default — node starts in PAIRING
        ON_CALL(*peer_mgr_, load_from_storage(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(etl::vector<PeerInfo, MAX_PEERS>{}));

        // submódule inits succeed by default
        ON_CALL(*tx_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*scanner_, init(_, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*heartbeat_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*pairing_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*tx_mgr_, get_task_handle()).WillByDefault(Return(nullptr));

        // hal_timer_: get_time_ms() is called by transport_worker_task via tick()
        ON_CALL(*hal_timer_, get_time_us()).WillByDefault(Return(0));

        // message_router_: by default packets go to rx_dispatch_task (not worker)
        ON_CALL(*message_router_, should_dispatch_to_worker(_)).WillByDefault(Return(false));

        sut_ = std::make_unique<EspNowManagerTestable>(
            std::move(hal_wifi),
            std::move(hal_timer),
            // RealFreeRTOSHAL — creates real tasks, queues and mutex in init()
            std::make_unique<RealFreeRTOSHAL>(),
            std::move(driver),
            std::move(peer_mgr),
            std::move(codec),
            std::move(scanner),
            std::move(tx_fsm),
            std::move(tx_mgr),
            std::move(heartbeat_mgr),
            std::move(pairing_mgr),
            std::move(message_router));
    }

    void TearDown() override
    {
        sut_->deinit();
        // Give tasks time to exit cleanly after deinit() signals stop
        vTaskDelay(pdMS_TO_TICKS(50));
        sut_.reset(); // destroy SUT before mocks
    }

    // -----------------------------------------------------------------------
    // Helper: init sut_ with a valid config and give tasks time to start.
    // -----------------------------------------------------------------------
    void init_and_wait()
    {
        EspNowConfig cfg{};
        cfg.node_id = kNodeId;
        cfg.node_type = kNodeType;
        cfg.wifi_channel = 1;
        cfg.app_rx_queue = xQueueCreate(10, sizeof(RxPacket)); // app queue

        ASSERT_EQ(sut_->init(cfg), ESP_OK);
        vTaskDelay(pdMS_TO_TICKS(kDelayMs)); // let tasks start and block
    }
};

// ===========================================================================
// rx_dispatch_task — NOTIFY_CHANNEL_FOUND
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ChannelFoundTransitionsToOperational)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    // on_channel_found_cb stores the channel and notifies rx_dispatch_task
    // via NOTIFY_CHANNEL_FOUND. The task then transitions NodeState.
    sut_->on_channel_found_cb(6);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
}

TEST_F(EspNowManagerTaskTest, ChannelFoundCallsPeerManagerPersist)
{
    init_and_wait();

    EXPECT_CALL(*peer_mgr_, persist()).Times(1);

    sut_->on_channel_found_cb(6);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

// ===========================================================================
// rx_dispatch_task — NOTIFY_SCAN_FAILED
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ScanFailedStaysInPairing)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    sut_->on_scan_failed_cb();
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    // SCAN_FAILED while in PAIRING keeps the node in PAIRING
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

// ===========================================================================
// rx_dispatch_task — NOTIFY_SCANNING
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ScanStartedTransitionsToScanning)
{
    init_and_wait();

    sut_->on_scan_started_cb();
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::SCANNING);
}

// ===========================================================================
// transport_worker_task — tick() called when PAIRING
// ===========================================================================

TEST_F(EspNowManagerTaskTest, WorkerTaskCallsPairingTickWhenInPairing)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    // tick() is called every loop iteration when NodeState == PAIRING.
    // Wait slightly longer than queue_receive timeout (100ms) to ensure
    // at least one idle loop iteration fires the tick.
    EXPECT_CALL(*pairing_mgr_, tick(_)).Times(AtLeast(1));
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

// ===========================================================================
// transport_worker_task — tick() NOT called when OPERATIONAL
// ===========================================================================

TEST_F(EspNowManagerTaskTest, WorkerTaskDoesNotCallPairingTickWhenOperational)
{
    init_and_wait();
    sut_->set_node_state(NodeState::OPERATIONAL);

    EXPECT_CALL(*pairing_mgr_, tick(_)).Times(0);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}
