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
static constexpr uint32_t delay_ms = 10;    // time to let tasks process
// Slightly longer than queue_receive timeout (100ms) to guarantee
// the rx_dispatch_task has completed at least one full loop iteration
// and processed any pending notifications.
// Must be > pdMS_TO_TICKS(100) — the queue_receive timeout in rx_dispatch_task
static constexpr uint32_t notify_delay_ms = 105;

static constexpr uint8_t rx_queue_length = 30;

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

    std::optional<MessageHeader> get_last_header_ack() const { return last_header_requiring_ack_; }

    // Expose protected members for testing
    using EspNowManager::rx_queue_handle_;
    using EspNowManager::rx_task_handle_;
};

// ---------------------------------------------------------------------------
// Fixture
//
// RealFreeRTOSHAL is injected so EspNowManager::init() creates real tasks,
// queues and mutex via create_task(), create_queue(), create_mutex().
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
        // ON_CALL(*heartbeat_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*pairing_mgr_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*tx_mgr_, get_task_handle()).WillByDefault(Return(nullptr));

        // hal_timer_: get_time_ms() is called by rx_task via tick()
        ON_CALL(*hal_timer_, get_time_us()).WillByDefault(Return(0));

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
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
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
        cfg.app_rx_queue = xQueueCreate(10, sizeof(AppMessage)); // app queue
        cfg.rx_queue_length = rx_queue_length;
        cfg.stack_size_rx_task = 2048;
        cfg.priority_rx_task = 5;
        cfg.stack_size_tx_task = 2048;
        cfg.priority_tx_task = 5;

        ASSERT_EQ(sut_->init(cfg), ESP_OK);
        vTaskDelay(pdMS_TO_TICKS(delay_ms)); // let tasks start and block
    }

    void receive_valid_rx_packet()
    {
        RxPacket packet{};
        packet.len = sizeof(MessageHeader) + 1;
        xQueueSend(sut_->rx_queue_handle_, &packet, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
};

// ===========================================================================
// rx_task — NOTIFY_CHANNEL_FOUND
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ChannelFoundStartsPairingForNode)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    EXPECT_CALL(*peer_mgr_, persist()).Times(1); // peer_manager::persist is called when channel is found
    EXPECT_CALL(*pairing_mgr_, start(_, _)).Times(1);

    // on_channel_found_cb stores the channel and notifies rx_task
    // via NOTIFY_CHANNEL_FOUND. The task then calls pairing_mgr_->start.
    sut_->on_channel_found_cb(6);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

// ===========================================================================
// rx_task — NOTIFY_SCAN_FAILED
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ScanFailedSendsToPairingState)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING); // Initial state when no peers are found on init
    sut_->set_node_state(NodeState::OPERATIONAL);          // Set to operational to test the callback

    sut_->on_scan_failed_cb();
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    // SCAN_FAILED while in OPERATIONAL sends the node to PAIRING
    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

// ===========================================================================
// rx_task — NOTIFY_SCANNING
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ScanStartedTransitionsToScanning)
{
    init_and_wait();

    sut_->on_scan_started_cb();
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::SCANNING);
}

// ===========================================================================
// rx_task — tick() called when PAIRING
// ===========================================================================

TEST_F(EspNowManagerTaskTest, RxTaskCallsPairingTickWhenInPairing)
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
// rx_task — tick() NOT called when OPERATIONAL
// ===========================================================================

TEST_F(EspNowManagerTaskTest, RxTaskDoesNotCallPairingTickWhenOperational)
{
    init_and_wait();
    sut_->set_node_state(NodeState::OPERATIONAL);

    EXPECT_CALL(*pairing_mgr_, tick(_)).Times(0);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

// ===========================================================================
// RxPackets tests
// ===========================================================================

TEST_F(EspNowManagerTaskTest, RxDispatchDoesNotRouteInvalidCrcPackets)
{
    init_and_wait();

    // codec must validate CRC and decode header successfully
    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(false));

    EXPECT_CALL(*message_router_, handle_packet(_)).Times(0); // No packet should be routed

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, RxDispatchDoesNotRouteIfCodecFailsToDecodeHeader)
{
    init_and_wait();

    // codec must validate CRC and decode header successfully
    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(std::nullopt));

    EXPECT_CALL(*message_router_, handle_packet(_)).Times(0); // No packet should be routed

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, PacketRequiringAckIsStored)
{
    init_and_wait();

    // codec must validate CRC and decode header successfully
    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    header.requires_ack = true;
    header.sender_node_id = kNodeId;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    // DATA type goes to app_rx_queue directly

    receive_valid_rx_packet();

    auto decoded_header = sut_->get_last_header_ack();
    EXPECT_TRUE(decoded_header.has_value());
    EXPECT_EQ(decoded_header->msg_type, MessageType::DATA);
    EXPECT_EQ(decoded_header->sender_node_id, kNodeId);
}

TEST_F(EspNowManagerTaskTest, RxDispatchTaskDropsPacketWhenQueueFull)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));

    // Normal packets
    MessageHeader header{};
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    vTaskSuspend(sut_->rx_task_handle_); // Suspend so it cannot consume from the queue before we check

    // Prepare RxPacket to send
    RxPacket packet{};
    packet.len = sizeof(MessageHeader) + 1;
    // Fill the queue completely
    for (int i = 0; i < rx_queue_length; ++i) {
        xQueueSend(sut_->rx_queue_handle_, &packet, 0);
    }
    // One extra packet should be dropped
    BaseType_t result = xQueueSend(sut_->rx_queue_handle_, &packet, 0);
    EXPECT_EQ(result, errQUEUE_FULL); // confirms it was rejected at queue level

    EXPECT_EQ(uxQueueMessagesWaiting(sut_->rx_queue_handle_), rx_queue_length);

    vTaskResume(sut_->rx_task_handle_);  // Resume so task can consume the packets
    vTaskDelay(pdMS_TO_TICKS(delay_ms)); // Lets wait to rx_task process

    EXPECT_EQ(uxQueueMessagesWaiting(sut_->rx_queue_handle_), 0);
}
