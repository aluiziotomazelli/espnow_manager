// host_test/test_tx_manager/main/test_tx_manager_task.cpp

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_tx_state_machine.hpp"
#include "mock_hal_espnow.hpp"
#include "mock_message_codec.hpp"
#include "mock_discovery_manager.hpp"
#include "mock_statistics_manager.hpp"
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
    std::unique_ptr<NiceMock<MockEspNowHAL>> hal_owned;
    std::unique_ptr<NiceMock<MockMessageCodec>> codec_owned;
    std::unique_ptr<NiceMock<MockStatisticsManager>> statistics_mgr_owned;

    // Raw pointers to use in tests
    NiceMock<MockTxStateMachine>* fsm;
    NiceMock<MockEspNowHAL>* hal;
    NiceMock<MockMessageCodec>* codec;
    NiceMock<MockStatisticsManager>* statistics_mgr;

    RealFreeRTOSHAL freertos_hal;
    std::unique_ptr<TxManager> manager;

    TxState current_state = TxState::IDLE;
    std::optional<PendingAck> pending_ack = std::nullopt;
    TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(0x6);

    uint32_t ack_timeout_ms = 50;
    uint32_t delay_ms = 20;

    void SetUp() override
    {
        fsm_owned = std::make_unique<NiceMock<MockTxStateMachine>>();
        hal_owned = std::make_unique<NiceMock<MockEspNowHAL>>();
        codec_owned = std::make_unique<NiceMock<MockMessageCodec>>();
        statistics_mgr_owned = std::make_unique<NiceMock<MockStatisticsManager>>();

        fsm = fsm_owned.get();
        hal = hal_owned.get();
        codec = codec_owned.get();
        statistics_mgr = statistics_mgr_owned.get();

        // FSM tracks state via local variable
        ON_CALL(*fsm, get_state()).WillByDefault(ReturnPointee(&current_state));
        ON_CALL(*fsm, on_packet_sent(_)).WillByDefault(Invoke([this](bool requires_ack) {
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
        ON_CALL(*fsm, on_delivery_failure()).WillByDefault(Return(false));
        ON_CALL(*fsm, on_max_retries()).WillByDefault(Invoke([this]() {
            current_state = TxState::IDLE;
            return current_state;
        }));
        ON_CALL(*fsm, on_link_alive()).WillByDefault(Invoke([this]() {
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

        // Codec defaults
        ON_CALL(*codec, encode(_, _, _, _, _)).WillByDefault(Return(10));

        manager = std::make_unique<TxManager>(
            *fsm_owned, *hal_owned, freertos_hal, *codec_owned, *statistics_mgr_owned, ack_timeout_ms);
    }

    void TearDown() override
    {
        manager->deinit();
        vTaskDelay(pdMS_TO_TICKS(50)); // give task time to exit cleanly
        manager.reset();               // destroy TxManager before mocks
        if (real_rx_task_handle) {
            vTaskDelete(real_rx_task_handle);
            real_rx_task_handle = nullptr;
        }
    }

    // Helper: init and give task time to start and block
    void init_and_wait()
    {
        ASSERT_EQ(ESP_OK, manager->init(4096, 5, fake_rx_task));
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    bool notify_max_failures = false;
    TaskHandle_t real_rx_task_handle = nullptr;
    void make_real_rx_task()
    {
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<TxManagerTaskTest*>(arg);
                uint32_t notifications = 0;
                while (true) {
                    if (xTaskNotifyWait(0, NOTIFY_ALL, &notifications, portMAX_DELAY)) {
                        if (notifications & NOTIFY_MAX_FAILURES) {
                            self->notify_max_failures = true;
                        }
                    }
                }
            },
            "RxTask",
            2048,
            this,
            5,
            &real_rx_task_handle);
    }

    // Helper: build a minimal DecodedTxPacket
    DecodedTxPacket make_packet(bool requires_ack = false)
    {
        DecodedTxPacket pkt = {};
        pkt.header.requires_ack = requires_ack;
        pkt.payload_len = 5;
        memset(pkt.dest_mac, 0xAA, 6);
        return pkt;
    }

    // Helper: build a PendingAck with retries left
    PendingAck make_pending_ack(uint8_t retries = 2)
    {
        PendingAck ack = {};
        ack.retries_left = retries;
        ack.packet.requires_ack = true;
        ack.packet.len = 10;
        memset(ack.packet.dest_mac, 0xAA, 6);
        return ack;
    }
};

// =============================================================================
// IDLE — queue packet and send
// =============================================================================

TEST_F(TxManagerTaskTest, IdleStateProcessesQueuedPacket)
{
    init_and_wait();

    EXPECT_CALL(*codec, encode(_, _, _, _, _)).Times(1);
    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).Times(1);

    manager->queue_packet(make_packet(false));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, IdleStateWithAckPacketTransitionsToWaitingForAck)
{
    init_and_wait();

    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);
}

TEST_F(TxManagerTaskTest, IdleStateWithNoAckPacketStaysIdle)
{
    init_and_wait();

    manager->queue_packet(make_packet(false));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state);
}

TEST_F(TxManagerTaskTest, InvalidStateCallsResetAndTransitionsToIdle)
{
    init_and_wait();

    current_state = TxState::COUNT; // Invalid state

    EXPECT_CALL(*fsm, reset()).Times(1);       // Must call reset()
    manager->queue_packet(make_packet(false)); // Trigger th FSM
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

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
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, IdleStateNotifyDeliveryFailureCallsFsmOnDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);
    EXPECT_CALL(stats_mgr, on_transmission_failure()).Times(1);

    manager->notify_delivery_failure();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, OnDeliveryFailureReturningTrueCallsNotifyMaxFailuresOnRxTask)
{
    // Create a real rx task defined on the test class
    make_real_rx_task();

    // Init with the real task handle
    ASSERT_EQ(ESP_OK, manager->init(4096, 5, real_rx_task_handle));
    vTaskDelay(pdMS_TO_TICKS(20));

    // On delivery failure returns true if the number of failures is greater than max_failures
    EXPECT_CALL(*fsm, on_delivery_failure()).WillOnce(Return(true));

    // Trigger the notification
    manager->notify_delivery_failure();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // Real rx task will be notifyed and turns the flag true
    EXPECT_TRUE(notify_max_failures);
}

TEST_F(TxManagerTaskTest, NotifyStopStopsTaskAndCleansTheTaskHandle)
{
    init_and_wait();

    // Get tx_task_handle
    TaskHandle_t tx_task_handle = manager->get_task_handle();

    // Manually notify the task to stop
    xTaskNotify(tx_task_handle, NOTIFY_TASK_TO_STOP, eSetBits);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // Task should break, stop, clear resources and set task handle to null
    EXPECT_EQ(nullptr, manager->get_task_handle());
}

// =============================================================================
// WAITING_FOR_ACK
// =============================================================================

TEST_F(TxManagerTaskTest, WaitingForAckNotifyLogicalAckCallsOnAckReceived)
{
    init_and_wait();

    // Put task in WAITING_FOR_ACK
    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    ASSERT_EQ(TxState::WAITING_FOR_ACK, current_state);

    EXPECT_CALL(*fsm, on_ack_timeout()).Times(0);  // Should not call on_ack_timeout
    EXPECT_CALL(*fsm, on_ack_received()).Times(1); // Should call on_ack_received

    // Simulate ACK packet arriving via handle_ack()
    // sequence_number = 0 because it's the first packet (sequence_counter_ starts at 0)
    DecodedRxPacket ack_packet = {};
    ack_packet.header.msg_type = MessageType::ACK;
    ack_packet.header.sequence_number = 0; // First packet sequence number
    ack_packet.header.ack_status = AckStatus::OK;
    manager->handle_ack(ack_packet);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state);
}

TEST_F(TxManagerTaskTest, WaitingForAckTimeoutCallsOnAckTimeout)
{
    init_and_wait();

    // Put task in WAITING_FOR_ACK
    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    ASSERT_EQ(TxState::WAITING_FOR_ACK, current_state);

    EXPECT_CALL(*fsm, on_ack_timeout()).Times(1);

    // Give time for the timer to expire
    vTaskDelay(pdMS_TO_TICKS(ack_timeout_ms + 20));
}

TEST_F(TxManagerTaskTest, WaitingForAckNotifyDeliveryFailureCallsOnDeliveryFailure)
{
    init_and_wait();

    manager->queue_packet(make_packet(true));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    ASSERT_EQ(TxState::WAITING_FOR_ACK, current_state);

    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);

    manager->notify_delivery_failure();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
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
    manager->notify_delivery_failure();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, RetryingWithEspnowErrorCallsFsmOnDeliveryFailure)
{
    init_and_wait();

    uint8_t retry_count = 5;
    pending_ack = make_pending_ack(retry_count); // One retry
    current_state = TxState::RETRYING;

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _))
        .Times(retry_count)
        .WillRepeatedly(Return(ESP_ERR_ESPNOW_CHAN)); // Send fail
    EXPECT_CALL(*fsm, on_delivery_failure())
        .Times(retry_count + 1); // + 1 for the inital fail that trigger the retry loop

    manager->notify_delivery_failure(); // trigger retry loop
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

// =============================================================================
// Statistics Reporting
// =============================================================================

TEST_F(TxManagerTaskTest, IdleStateProcessesPacketAndReportsSent)
{
    init_and_wait();

    EXPECT_CALL(*statistics_mgr, on_packet_sent(_, _)).Times(1);

    manager->queue_packet(make_packet(false));
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, RetryingResendsPacketAndReportsRetry)
{
    init_and_wait();
    
    // Setup state: RETRYING
    pending_ack = make_pending_ack(2);
    current_state = TxState::RETRYING;

    // Expectation: stats manager is called on retry
    EXPECT_CALL(*statistics_mgr, on_retry(_)).Times(1);

    manager->notify_delivery_failure();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, MaxRetriesExhaustedReportsPacketLost)
{
    init_and_wait();

    // Setup state: RETRYING
    pending_ack = make_pending_ack(0); // No retries left
    current_state = TxState::RETRYING;

    // Expectation: stats manager reports loss
    EXPECT_CALL(*statistics_mgr, on_packet_lost(_)).Times(1);

    manager->notify_delivery_failure();
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

// =============================================================================
// TxManager::handle_esp_now_send_errors(esp_err_t error)
// =============================================================================

TEST_F(TxManagerTaskTest, EspnowNoMemoryErrorDoesNotCallDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_NO_MEM)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);    // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(0); // Should not call on_delivery_failure

    manager->queue_packet(make_packet(true)); // Call with pending ack == true
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}

TEST_F(TxManagerTaskTest, EspnowNotInitiErrorDoesNotCallDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_NOT_INIT)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);    // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(0); // Should not call on_delivery_failure

    manager->queue_packet(make_packet(true)); // Call with pending ack == true
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}

TEST_F(TxManagerTaskTest, EspnowArgErrorDoesNotCallDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_ARG)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);                                    // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(0); // Should not call on_delivery_failure

    manager->queue_packet(make_packet(true)); // Call with pending ack == true
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}

TEST_F(TxManagerTaskTest, EspnowOtherErrorCallsDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_CHAN)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);                                     // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);                                  // Must call on_delivery_failure

    manager->queue_packet(make_packet(true)); // Call with pending ack == true
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}
