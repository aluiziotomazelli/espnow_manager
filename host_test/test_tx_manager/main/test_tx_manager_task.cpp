// host_test/test_tx_manager/main/test_tx_manager_task.cpp

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_tx_state_machine.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_message_codec.hpp"
#include "mock_channel_scanner.hpp"
#include "hal_real_freertos.hpp"
#include "tx_manager.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SaveArg;

// =============================================================================
// Fixture
// =============================================================================

class TxManagerTaskTest : public ::testing::Test
{
protected:
    // Owned pointers to correct destruction and no mock leakage on tests
    std::unique_ptr<NiceMock<MockTxStateMachine>> fsm_owned;
    std::unique_ptr<NiceMock<MockWiFiHAL>> hal_owned;
    std::unique_ptr<NiceMock<MockMessageCodec>> codec_owned;
    std::unique_ptr<NiceMock<MockChannelScanner>> scanner_owned;

    // Raw pointers to use in tests
    NiceMock<MockTxStateMachine> *fsm;
    NiceMock<MockWiFiHAL> *hal;
    NiceMock<MockMessageCodec> *codec;
    NiceMock<MockChannelScanner> *scanner;

    RealFreeRTOSHAL freertos_hal;
    std::unique_ptr<TxManager> manager;

    TxState current_state = TxState::IDLE;
    std::optional<PendingAck> pending_ack = std::nullopt;

    void SetUp() override
    {
        fsm_owned = std::make_unique<NiceMock<MockTxStateMachine>>();
        hal_owned = std::make_unique<NiceMock<MockWiFiHAL>>();
        codec_owned = std::make_unique<NiceMock<MockMessageCodec>>();
        scanner_owned = std::make_unique<NiceMock<MockChannelScanner>>();

        // auto fsm_owned = std::make_unique<NiceMock<MockTxStateMachine>>();
        // auto hal_owned = std::make_unique<NiceMock<MockWiFiHAL>>();
        // auto codec_owned = std::make_unique<NiceMock<MockMessageCodec>>();
        // auto scanner_owned = std::make_unique<NiceMock<MockChannelScanner>>();

        fsm = fsm_owned.get();
        hal = hal_owned.get();
        codec = codec_owned.get();
        scanner = scanner_owned.get();

        // FSM tracks state via local variable
        ON_CALL(*fsm, get_state()).WillByDefault(ReturnPointee(&current_state));
        ON_CALL(*fsm, on_tx_success(_)).WillByDefault(Invoke([this](bool requires_ack) {
            current_state = requires_ack ? TxState::WAITING_FOR_ACK : TxState::IDLE;
            return current_state;
        }));
        ON_CALL(*fsm, on_ack_received()).WillByDefault(Invoke([this]() {
            current_state = TxState::IDLE;
            return current_state;
        }));
        ON_CALL(*fsm, on_ack_timeout()).WillByDefault(Invoke([this]() {
            current_state = TxState::RETRYING;
            return current_state;
        }));
        ON_CALL(*fsm, on_physical_fail()).WillByDefault(Invoke([this]() {
            return current_state; // stays in current state unless threshold reached
        }));
        ON_CALL(*fsm, on_max_retries()).WillByDefault(Invoke([this]() {
            current_state = TxState::IDLE;
            return current_state;
        }));
        ON_CALL(*fsm, reset()).WillByDefault(Invoke([this]() {
            current_state = TxState::IDLE;
            pending_ack = std::nullopt;
        }));
        ON_CALL(*fsm, get_pending_ack()).WillByDefault(ReturnPointee(&pending_ack));
        ON_CALL(*fsm, set_pending_ack(_)).WillByDefault(SaveArg<0>(&pending_ack));

        // HAL defaults
        ON_CALL(*hal, hal_esp_now_send(_, _, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(*hal, wifi_get_channel(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(*hal, wifi_set_channel(_)).WillByDefault(Return(ESP_OK));

        // Codec defaults
        ON_CALL(*codec, calculate_crc(_, _)).WillByDefault(Return(0xAB));

        // Scanner defaults — hub not found
        ON_CALL(*scanner, scan(_)).WillByDefault(Return(IChannelScanner::ScanResult{1, false}));

        manager = std::make_unique<TxManager>(*fsm_owned, *scanner_owned, *hal_owned, freertos_hal, *codec_owned);
    }

    void TearDown() override
    {
        manager->deinit();
        vTaskDelay(pdMS_TO_TICKS(150)); // give task time to exit cleanly
        manager.reset();                // destroy TxManager before mocks
        // Mocks are destroyed automatically
    }

    // Helper: init and give task time to start and block
    void init_and_wait()
    {
        ASSERT_EQ(ESP_OK, manager->init(4096, 5));
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Helper: build a minimal TxPacket
    TxPacket make_packet(bool requires_ack = false)
    {
        TxPacket pkt = {};
        pkt.requires_ack = requires_ack;
        pkt.len = sizeof(MessageHeader) + 1; // header + CRC byte
        return pkt;
    }

    // Helper: build a PendingAck with retries left
    PendingAck make_pending_ack(uint8_t retries = 2)
    {
        PendingAck ack = {};
        ack.retries_left = retries;
        ack.packet = make_packet(true);
        return ack;
    }
};

// =============================================================================
// IDLE — queue packet and send
// =============================================================================

TEST_F(TxManagerTaskTest, IdleStateProcessesQueuedPacket)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).Times(1);

    manager->queue_packet(make_packet(false));
    vTaskDelay(pdMS_TO_TICKS(50));
}

TEST_F(TxManagerTaskTest, IdleStateWithAckPacketTransitionsToWaitingForAck)
{
    init_and_wait();

    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(50));

    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);
}

TEST_F(TxManagerTaskTest, IdleStateWithNoAckPacketStaysIdle)
{
    init_and_wait();

    manager->queue_packet(make_packet(false));
    vTaskDelay(pdMS_TO_TICKS(50));

    EXPECT_EQ(TxState::IDLE, current_state);
}

// =============================================================================
// IDLE — notifications
// =============================================================================

TEST_F(TxManagerTaskTest, IdleStateNotifyLinkAliveCallsFsmOnLinkAlive)
{
    init_and_wait();

    EXPECT_CALL(*fsm, on_link_alive()).Times(1);

    manager->notify_link_alive();
    vTaskDelay(pdMS_TO_TICKS(50));
}

TEST_F(TxManagerTaskTest, IdleStateNotifyPhysicalFailCallsFsmOnPhysicalFail)
{
    init_and_wait();

    EXPECT_CALL(*fsm, on_physical_fail()).Times(1);

    manager->notify_physical_fail();
    vTaskDelay(pdMS_TO_TICKS(50));
}

// =============================================================================
// WAITING_FOR_ACK
// =============================================================================

TEST_F(TxManagerTaskTest, WaitingForAckNotifyLogicalAckCallsOnAckReceived)
{
    init_and_wait();

    // Put task in WAITING_FOR_ACK
    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(50));
    ASSERT_EQ(TxState::WAITING_FOR_ACK, current_state);

    EXPECT_CALL(*fsm, on_ack_received()).Times(1);

    manager->notify_logical_ack();
    vTaskDelay(pdMS_TO_TICKS(50));

    EXPECT_EQ(TxState::IDLE, current_state);
}

TEST_F(TxManagerTaskTest, WaitingForAckNotifyPhysicalFailCallsOnPhysicalFail)
{
    init_and_wait();

    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(50));
    ASSERT_EQ(TxState::WAITING_FOR_ACK, current_state);

    EXPECT_CALL(*fsm, on_physical_fail()).Times(1);

    manager->notify_physical_fail();
    vTaskDelay(pdMS_TO_TICKS(50));
}

// =============================================================================
// RETRYING
// =============================================================================

TEST_F(TxManagerTaskTest, RetryingWithPendingAckResendsPacket)
{
    init_and_wait();

    // Set up RETRYING state with pending ack
    pending_ack = make_pending_ack(2);
    current_state = TxState::RETRYING;

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).Times(1);

    // Trigger loop iteration via notify
    manager->notify_physical_fail();
    vTaskDelay(pdMS_TO_TICKS(50));
}

TEST_F(TxManagerTaskTest, RetryingWithNoPendingAckCallsOnMaxRetries)
{
    init_and_wait();

    pending_ack = std::nullopt;
    current_state = TxState::RETRYING;

    EXPECT_CALL(*fsm, on_max_retries()).Times(1);

    manager->notify_physical_fail();
    vTaskDelay(pdMS_TO_TICKS(50));
}

// =============================================================================
// SCANNING
// =============================================================================

TEST_F(TxManagerTaskTest, ScanningStateCallsScannerAndResetsOnHubNotFound)
{
    init_and_wait();

    current_state = TxState::SCANNING;

    EXPECT_CALL(*scanner, scan(_)).Times(1).WillOnce(Return(IChannelScanner::ScanResult{6, false}));
    EXPECT_CALL(*fsm, reset()).Times(1);

    manager->notify_physical_fail(); // trigger loop
    vTaskDelay(pdMS_TO_TICKS(50));
}

TEST_F(TxManagerTaskTest, ScanningStateWithHubFoundSetsChannelAndCallsOnLinkAlive)
{
    init_and_wait();

    current_state = TxState::SCANNING;

    ON_CALL(*scanner, scan(_)).WillByDefault(Return(IChannelScanner::ScanResult{9, true}));

    EXPECT_CALL(*hal, wifi_set_channel(9)).Times(1);
    EXPECT_CALL(*fsm, on_link_alive()).Times(1);

    manager->notify_physical_fail();
    vTaskDelay(pdMS_TO_TICKS(50));
}