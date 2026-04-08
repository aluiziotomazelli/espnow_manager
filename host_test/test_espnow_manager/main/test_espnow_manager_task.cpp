// host_test/test_espnow_manager/main/test_espnow_manager_task.cpp
//
// Task-based tests — real FreeRTOS tasks are created by EspNowManager::init()
// via the injected RealFreeRTOSHAL. All hardware-dependent components
// (WiFi, Timer, ESP-NOW driver) remain mocked since the host has no hardware.
//
// NodeStateMachine is also mocked (MockNodeStateMachine) — the mock constructor
// registers ON_CALL defaults that mirror real FSM transitions, so state advances
// naturally without needing the real implementation. Individual tests can
// override specific ON_CALL entries when they need non-default behaviour.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mock_espnow_driver.hpp"
#include "mock_discovery_manager.hpp"
#include "mock_hal_timer.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_hal_espnow.hpp"
#include "mock_heartbeat_manager.hpp"
#include "mock_message_codec.hpp"
#include "mock_message_router.hpp"
#include "mock_node_state_machine.hpp"
#include "mock_pairing_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_storage_manager.hpp"
#include "mock_channel_monitor.hpp"
#include "mock_tx_manager.hpp"
#include "mock_tx_state_machine.hpp"
#include "mock_statistics_manager.hpp"
#include "hal_real_freertos.hpp"

#include "espnow_manager.hpp"
#include "protocol_types.hpp"

using ::testing::_;
using ::testing::AtLeast;
using ::testing::NiceMock;
using ::testing::Return;

// ---------------------------------------------------------------------------
// Test constants
// ---------------------------------------------------------------------------
static constexpr NodeId kNodeId = 0x05;
static constexpr NodeId kHubId = ReservedIds::HUB;
static constexpr NodeType kNodeType = 0x02; // non-HUB
static constexpr PayloadType kPayloadType = 0x02;
static constexpr uint32_t delay_ms = 10;

// Slightly longer than queue_receive timeout (100ms) to guarantee
// the rx_task has completed at least one full loop iteration and
// processed any pending notifications.
static constexpr uint32_t notify_delay_ms = 105;

static constexpr uint8_t rx_queue_length = 30;

// ---------------------------------------------------------------------------
// Testable subclass — exposes internal handles for direct test manipulation
// ---------------------------------------------------------------------------
class EspNowManagerTestable : public EspNowManager
{
public:
    using EspNowManager::EspNowManager;
    using EspNowManager::node_fsm_;
    using EspNowManager::rx_queue_handle_;
    using EspNowManager::rx_task_handle_;
};

// ---------------------------------------------------------------------------
// Fixture
//
// RealFreeRTOSHAL is injected so EspNowManager::init() creates real tasks,
// queues and mutex. MockNodeStateMachine is used instead of the real FSM —
// its constructor registers ON_CALL defaults that mirror real transitions,
// so tests that call  node_fsm_->set_state(nodestate::operational);() or trigger notifications
// get realistic state progression without depending on the real FSM.
//
// TearDown calls deinit() which signals tasks to stop and waits up to 1s
// for graceful exit before force-deleting them.
// ---------------------------------------------------------------------------
class EspNowManagerTaskTest : public ::testing::Test
{
protected:
    NiceMock<MockStorageManager>* storage_;
    NiceMock<MockEspNowDriver>* driver_;
    NiceMock<MockTimerHAL>* hal_timer_;
    NiceMock<MockWiFiHAL>* hal_wifi_;
    NiceMock<MockEspNowHAL>* hal_espnow_;
    NiceMock<MockPeerManager>* peer_mgr_;
    NiceMock<MockMessageCodec>* codec_;
    NiceMock<MockChannelMonitor>* channel_monitor_;
    NiceMock<MockDiscoveryManager>* scanner_;
    NiceMock<MockTxStateMachine>* tx_fsm_;
    NiceMock<MockTxManager>* tx_mgr_;
    NiceMock<MockHeartbeatManager>* heartbeat_mgr_;
    NiceMock<MockPairingManager>* pairing_mgr_;
    NiceMock<MockMessageRouter>* message_router_;
    NiceMock<MockNodeStateMachine>* node_fsm_;
    NiceMock<MockStatisticsManager>* stats_mgr_;

    std::unique_ptr<EspNowManagerTestable> sut_;

    QueueHandle_t app_queue_handle;

    void SetUp() override
    {
        auto storage = std::make_unique<NiceMock<MockStorageManager>>();
        auto driver = std::make_unique<NiceMock<MockEspNowDriver>>();
        auto hal_timer = std::make_unique<NiceMock<MockTimerHAL>>();
        auto hal_wifi = std::make_unique<NiceMock<MockWiFiHAL>>();
        auto hal_espnow = std::make_unique<NiceMock<MockEspNowHAL>>();
        auto peer_mgr = std::make_unique<NiceMock<MockPeerManager>>();
        auto codec = std::make_unique<NiceMock<MockMessageCodec>>();
        auto channel_monitor = std::make_unique<NiceMock<MockChannelMonitor>>();
        auto scanner = std::make_unique<NiceMock<MockDiscoveryManager>>();
        auto tx_fsm = std::make_unique<NiceMock<MockTxStateMachine>>();
        auto tx_mgr = std::make_unique<NiceMock<MockTxManager>>();
        auto heartbeat_mgr = std::make_unique<NiceMock<MockHeartbeatManager>>();
        auto pairing_mgr = std::make_unique<NiceMock<MockPairingManager>>();
        auto message_router = std::make_unique<NiceMock<MockMessageRouter>>();
        auto stats_mgr = std::make_unique<NiceMock<MockStatisticsManager>>();
        auto node_fsm = std::make_unique<NiceMock<MockNodeStateMachine>>();

        storage_ = storage.get();
        driver_ = driver.get();
        hal_timer_ = hal_timer.get();
        hal_wifi_ = hal_wifi.get();
        hal_espnow_ = hal_espnow.get();
        peer_mgr_ = peer_mgr.get();
        codec_ = codec.get();
        channel_monitor_ = channel_monitor.get();
        scanner_ = scanner.get();
        tx_fsm_ = tx_fsm.get();
        tx_mgr_ = tx_mgr.get();
        heartbeat_mgr_ = heartbeat_mgr.get();
        pairing_mgr_ = pairing_mgr.get();
        message_router_ = message_router.get();
        stats_mgr_ = stats_mgr.get();
        node_fsm_ = node_fsm.get();

        // EspNowDriver succeeds — no real WiFi on host
        ON_CALL(*driver_, init(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*driver_, deinit()).WillByDefault(Return(ESP_OK));

        // Storage defaults
        ON_CALL(*storage_, load_channel(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(*storage_, store_channel(_)).WillByDefault(Return(ESP_OK));

        // peer_mgr: empty list → init transitions to PAIRING_SCAN
        ON_CALL(*peer_mgr_, load_peers_from_storage()).WillByDefault(Return(ESP_OK));
        ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(etl::vector<PeerInfo, MAX_PEERS>{}));

        // Submodule inits succeed
        ON_CALL(*tx_mgr_, init(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*scanner_, init(_, _, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*pairing_mgr_, init(_, _, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*channel_monitor_, init(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*tx_mgr_, get_task_handle()).WillByDefault(Return(nullptr));

        // Timer: get_time_us() called by rx_task via tick()
        ON_CALL(*hal_timer_, get_time_us()).WillByDefault(Return(0));

        sut_ = std::make_unique<EspNowManagerTestable>(
            std::move(storage),
            std::move(hal_wifi),
            std::move(hal_timer),
            std::make_unique<RealFreeRTOSHAL>(), // real FreeRTOS — creates actual tasks
            std::move(hal_espnow),
            std::move(driver),
            std::move(peer_mgr),
            std::move(codec),
            std::move(channel_monitor),
            std::move(scanner),
            std::move(tx_fsm),
            std::move(tx_mgr),
            std::move(heartbeat_mgr),
            std::move(pairing_mgr),
            std::move(message_router),
            std::move(stats_mgr),
            std::move(node_fsm));
    }

    void TearDown() override
    {
        if (sut_->rx_queue_handle_ != nullptr) {
            xQueueReset(sut_->rx_queue_handle_); // drena pacotes pendentes
        }
        sut_->deinit();
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        sut_.reset();
        if (app_queue_handle != nullptr) {
            vQueueDelete(app_queue_handle);
            app_queue_handle = nullptr;
        }
    }

    // -----------------------------------------------------------------------
    // Helper: init with a valid non-HUB config and let tasks start
    // -----------------------------------------------------------------------
    void init_and_wait()
    {
        EspNowConfig cfg{};
        cfg.node_id = kNodeId;
        cfg.node_type = kNodeType;
        cfg.wifi_channel = 1;
        app_queue_handle = xQueueCreate(10, sizeof(AppMessage));
        if (app_queue_handle == nullptr) {
            ADD_FAILURE() << "xQueueCreate failed for app_rx_queue";
        }
        cfg.app_rx_queue = app_queue_handle;
        cfg.rx_queue_length = rx_queue_length;
        cfg.stack_size_rx_task = 2048;
        cfg.priority_rx_task = 5;
        cfg.stack_size_tx_task = 2048;
        cfg.priority_tx_task = 5;

        esp_err_t err = sut_->init(cfg);
        if (err != ESP_OK) {
            ADD_FAILURE() << "sut_->init(cfg) failed: " << esp_err_to_name(err);
        }
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }

    // -----------------------------------------------------------------------
    // Helper: enqueue a minimal RxPacket and wait for the rx_task to process it
    // -----------------------------------------------------------------------
    void receive_valid_rx_packet(int8_t rssi = 0)
    {
        RxPacket packet{};
        packet.len = sizeof(MessageHeader) + CRC_SIZE;
        packet.rssi = rssi;
        // packet.len = sizeof(MessageHeader) + 1;
        xQueueSend(sut_->rx_queue_handle_, &packet, 0);
        vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
    }

    void send_notification_to_rx_task(uint32_t notification)
    {
        xTaskNotify(sut_->rx_task_handle_, notification, eSetBits);
    }
};

// ===========================================================================
// rx_task — NOTIFY_CHANNEL_CHANGED
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ChannelChangedUpdatesConfigAndStorage)
{
    init_and_wait();

    uint8_t new_channel = 11;
    // On channel change notification, EspNowManager updates its channel getting the new channel from ChannelMonitor
    ON_CALL(*channel_monitor_, get_wifi_channel()).WillByDefault(Return(new_channel));

    EXPECT_CALL(*storage_, store_channel(new_channel)).Times(1);
    EXPECT_CALL(*scanner_, set_channel(new_channel)).Times(1);

    // ChannelMonitor send a direct to task notification when detects a channel change
    send_notification_to_rx_task(NOTIFY_CHANNEL_CHANGED);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

// ===========================================================================
// rx_task — tick() behaviour per NodeState
// ===========================================================================

TEST_F(EspNowManagerTaskTest, RxTaskCallsPairingTickWhenPairing)
{
    init_and_wait();
    // After init with no peers: PAIRING_SCAN
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);

    // Advance to PAIRING so pairing_mgr_->tick() is exercised
    sut_->node_fsm_->on_channel_found(); // PAIRING_SCAN → PAIRING
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    EXPECT_CALL(*pairing_mgr_, tick(_)).Times(AtLeast(1));
    EXPECT_CALL(*channel_monitor_, tick(_)).Times(AtLeast(1));
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

TEST_F(EspNowManagerTaskTest, RxTaskCallsChannelMonitorTickWhenOperational)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    ASSERT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    EXPECT_CALL(*channel_monitor_, tick(_)).Times(AtLeast(1));
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

TEST_F(EspNowManagerTaskTest, RxTaskCallsChannelMonitorTickWhenPairing)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::PAIRING);

    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    EXPECT_CALL(*channel_monitor_, tick(_)).Times(AtLeast(1));
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

TEST_F(EspNowManagerTaskTest, RxTaskDoesNotCallsChannelMonitorTickWhenPairingScan)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::PAIRING_SCAN);

    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);

    EXPECT_CALL(*channel_monitor_, tick(_)).Times(0);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

TEST_F(EspNowManagerTaskTest, RxTaskCallsHeartbeatTickWhenOperational)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    ASSERT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    // Let rx_task complete one full loop from the PAIRING_SCAN phase first
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
    ::testing::Mock::VerifyAndClearExpectations(heartbeat_mgr_);
    ::testing::Mock::VerifyAndClearExpectations(pairing_mgr_);

    EXPECT_CALL(*heartbeat_mgr_, tick(_)).Times(AtLeast(1));
    EXPECT_CALL(*pairing_mgr_, tick(_)).Times(0);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

TEST_F(EspNowManagerTaskTest, RxTaskDoesNotCallPairingTickWhenOperational)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // Wait for rx_task to complete the queue_receive timeout and observe new state
    vTaskDelay(pdMS_TO_TICKS(500));
    ::testing::Mock::VerifyAndClearExpectations(pairing_mgr_);

    EXPECT_CALL(*pairing_mgr_, tick(_)).Times(0);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

// ===========================================================================
// rx_task — NOTIFY_PAIRING_DONE
// ===========================================================================

TEST_F(EspNowManagerTaskTest, PairingDoneWithNoPeersTransitionsToIdle)
{
    init_and_wait();

    // Advance to PAIRING so on_pairing_timeout is valid
    sut_->node_fsm_->on_channel_found(); // PAIRING_SCAN → PAIRING
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    // No peers — pairing failed
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(etl::vector<PeerInfo, MAX_PEERS>{}));

    xTaskNotify(sut_->rx_task_handle_, NOTIFY_PAIRING_DONE, eSetBits);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::IDLE);
}

TEST_F(EspNowManagerTaskTest, PairingDoneWithPeersTransitionsToOperational)
{
    init_and_wait();

    sut_->node_fsm_->on_channel_found(); // PAIRING_SCAN → PAIRING
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING);

    // Has peers — pairing succeeded
    etl::vector<PeerInfo, MAX_PEERS> peers;
    PeerInfo p{};
    p.node_id = kHubId;
    peers.push_back(p);
    ON_CALL(*peer_mgr_, get_all()).WillByDefault(Return(peers));

    send_notification_to_rx_task(NOTIFY_PAIRING_DONE);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
}

// ===========================================================================
// rx_task — NOTIFY_TASK_TO_STOP
// ===========================================================================

TEST_F(EspNowManagerTaskTest, NotifyStopDeletesRxTasAndClearHandle)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    send_notification_to_rx_task(NOTIFY_TASK_TO_STOP);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->rx_task_handle_, nullptr);
}

// ===========================================================================
// rx_task — NOTIFY_CHANNEL_FOUND
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ChannelFoundFromPairingScanTransitionsToPairing)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);

    // DiscoveryManager signals channel found
    ON_CALL(*scanner_, get_channel()).WillByDefault(Return(6));

    send_notification_to_rx_task(NOTIFY_CHANNEL_FOUND);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::PAIRING);
}

TEST_F(EspNowManagerTaskTest, ChannelFoundCallsPairingManagerStart)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);

    ON_CALL(*scanner_, get_channel()).WillByDefault(Return(6));
    ON_CALL(*scanner_, is_scanning()).WillByDefault(Return(false));

    EXPECT_CALL(*pairing_mgr_, start(_, _)).Times(1);

    send_notification_to_rx_task(NOTIFY_CHANNEL_FOUND);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));
}

TEST_F(EspNowManagerTaskTest, ChannelFoundFromRecoveryScanTransitionsToOperational)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::OPERATIONAL);

    // Simulate link loss → RECOVERY_SCAN
    sut_->node_fsm_->on_scan_requested();
    ASSERT_EQ(sut_->get_node_state(), NodeState::RECOVERY_SCAN);

    ON_CALL(*scanner_, get_channel()).WillByDefault(Return(6));

    send_notification_to_rx_task(NOTIFY_CHANNEL_FOUND);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);
}

// ===========================================================================
// rx_task — NOTIFY_SCAN_FAILED
// ===========================================================================

TEST_F(EspNowManagerTaskTest, ScanFailedTransitionsToIdle)
{
    init_and_wait();
    ASSERT_EQ(sut_->get_node_state(), NodeState::PAIRING_SCAN);

    send_notification_to_rx_task(NOTIFY_SCAN_FAILED);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::IDLE);
}

// ===========================================================================
// rx_task — NOTIFY_MAX_FAILURES
// ===========================================================================

TEST_F(EspNowManagerTaskTest, MaxFailuresFromOperationalTransitionsToRecoveryScan)
{
    init_and_wait();
    node_fsm_->set_state(NodeState::OPERATIONAL);
    ASSERT_EQ(sut_->get_node_state(), NodeState::OPERATIONAL);

    EXPECT_CALL(*scanner_, start_scan()).Times(1);

    send_notification_to_rx_task(NOTIFY_MAX_FAILURES);
    vTaskDelay(pdMS_TO_TICKS(notify_delay_ms));

    EXPECT_EQ(sut_->get_node_state(), NodeState::RECOVERY_SCAN);
}

// ===========================================================================
// rx_task — RxPacket processing
// ===========================================================================

TEST_F(EspNowManagerTaskTest, InvalidCrcPacketIsNotRouted)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(false));
    EXPECT_CALL(*message_router_, handle_packet(_)).Times(0);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, FailedHeaderDecodeIsNotRouted)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(std::nullopt));
    EXPECT_CALL(*message_router_, handle_packet(_)).Times(0);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, DataPacketRequiringAckDeliveredToAppQueueWithRequiresAckFlag)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    header.requires_ack = true;
    header.sender_node_id = kNodeId;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    receive_valid_rx_packet();

    AppMessage msg{};
    EXPECT_EQ(xQueueReceive(app_queue_handle, &msg, pdMS_TO_TICKS(50)), pdTRUE);
    EXPECT_TRUE(msg.requires_ack);
    EXPECT_EQ(msg.sender_id, kNodeId);
}

TEST_F(EspNowManagerTaskTest, ProtocolPacketIsRoutedViaMessageRouter)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::HEARTBEAT; // protocol-internal type
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    EXPECT_CALL(*message_router_, handle_packet(_)).Times(1);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, ValidPacketNotifiesTxManagerLinkAlive)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    EXPECT_CALL(*tx_mgr_, notify_link_alive()).Times(AtLeast(1));

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, QueueFullDropsExtraPackets)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    // Suspend rx_task so it cannot drain the queue while we fill it
    vTaskSuspend(sut_->rx_task_handle_);

    RxPacket packet{};
    packet.len = sizeof(MessageHeader) + 1;
    for (int i = 0; i < rx_queue_length; ++i) {
        xQueueSend(sut_->rx_queue_handle_, &packet, 0);
    }

    // One extra must be rejected at queue level
    BaseType_t result = xQueueSend(sut_->rx_queue_handle_, &packet, 0);
    EXPECT_EQ(result, errQUEUE_FULL);
    EXPECT_EQ(uxQueueMessagesWaiting(sut_->rx_queue_handle_), rx_queue_length);

    vTaskResume(sut_->rx_task_handle_);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(uxQueueMessagesWaiting(sut_->rx_queue_handle_), 0);
}

// ===========================================================================
// Behavior scenarios
// ===========================================================================

TEST_F(EspNowManagerTaskTest, DataPacketIsNotRouted)
{
    init_and_wait();

    // DATA packets should go to app_rx_queue, never to the router
    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    EXPECT_CALL(*message_router_, handle_packet(_)).Times(0);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, CommandPacketIsNotRouted)
{
    init_and_wait();

    // DATA packets should go to app_rx_queue, never to the router
    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::COMMAND;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    EXPECT_CALL(*message_router_, handle_packet(_)).Times(0);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, DataPacketIsDeliveredToAppQueue)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    header.sender_node_id = kNodeId;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    receive_valid_rx_packet();

    AppMessage msg{};
    EXPECT_EQ(xQueueReceive(app_queue_handle, &msg, pdMS_TO_TICKS(50)), pdTRUE);
    EXPECT_EQ(msg.sender_id, kNodeId);
    EXPECT_EQ(msg.payload_len, 0);
    EXPECT_EQ(msg.msg_type, MessageType::DATA);
}

TEST_F(EspNowManagerTaskTest, CommandPacketIsDeliveredToAppQueue)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::COMMAND;
    header.sender_node_id = kNodeId;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    receive_valid_rx_packet();

    AppMessage msg{};
    EXPECT_EQ(xQueueReceive(app_queue_handle, &msg, pdMS_TO_TICKS(50)), pdTRUE);
    EXPECT_EQ(msg.sender_id, kNodeId);
    EXPECT_EQ(msg.payload_len, 0);
    EXPECT_EQ(msg.msg_type, MessageType::COMMAND);
}

TEST_F(EspNowManagerTaskTest, DataPacketWithRequiresAckDeliveredToAppQueueAndStoresHeader)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    header.requires_ack = true;
    header.sender_node_id = kNodeId;
    header.sequence_number = 42;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    receive_valid_rx_packet();

    // Verify delivered to app queue
    AppMessage msg{};
    EXPECT_EQ(xQueueReceive(app_queue_handle, &msg, pdMS_TO_TICKS(50)), pdTRUE);
    EXPECT_EQ(msg.sender_id, kNodeId);
    EXPECT_EQ(msg.sequence_number, 42);
    EXPECT_TRUE(msg.requires_ack);
}

TEST_F(EspNowManagerTaskTest, ProtocolPacketDoesNotReachAppQueue)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::HEARTBEAT;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    receive_valid_rx_packet();

    // App queue must remain empty — protocol packets go to router only
    AppMessage msg{};
    EXPECT_EQ(xQueueReceive(app_queue_handle, &msg, pdMS_TO_TICKS(50)), pdFALSE);
}

TEST_F(EspNowManagerTaskTest, DataPacketIncludesRssiInAppMessage)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    header.sender_node_id = kNodeId;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    receive_valid_rx_packet(-42); // Simulate RSSI of -42 dBm

    // Verify RSSI is delivered to app queue
    AppMessage msg{};
    EXPECT_EQ(xQueueReceive(app_queue_handle, &msg, pdMS_TO_TICKS(50)), pdTRUE);
    EXPECT_EQ(-42, msg.rssi);
}

TEST_F(EspNowManagerTaskTest, ValidPacketCallsOnPacketReceived)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    MessageHeader header{};
    header.msg_type = MessageType::DATA;
    header.sender_node_id = kNodeId;
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(header));

    EXPECT_CALL(*stats_mgr_, on_packet_received(kNodeId, _, _)).Times(1);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, InvalidCrcDoesNotCallOnPacketReceived)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(false));

    EXPECT_CALL(*stats_mgr_, on_packet_received(_, _, _)).Times(0);

    receive_valid_rx_packet();
}

TEST_F(EspNowManagerTaskTest, FailedHeaderDecodeDoesNotCallOnPacketReceived)
{
    init_and_wait();

    ON_CALL(*codec_, validate_crc(_, _)).WillByDefault(Return(true));
    ON_CALL(*codec_, decode_header(_, _)).WillByDefault(Return(std::nullopt));

    EXPECT_CALL(*stats_mgr_, on_packet_received(_, _, _)).Times(0);

    receive_valid_rx_packet();
}