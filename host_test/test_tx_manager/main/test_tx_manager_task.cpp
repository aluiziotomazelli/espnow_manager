// host_test/test_tx_manager/main/test_tx_manager_task.cpp

#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_tx_state_machine.hpp"
#include "mock_en_hal_espnow.hpp"
#include "mock_message_codec.hpp"
#include "mock_discovery_manager.hpp"
#include "mock_statistics_manager.hpp"
#include "mock_peer_manager.hpp"
#include "mock_en_hal_timer.hpp"
#include "hal_real_freertos.hpp"
#include "tx_manager.hpp"
using namespace espnow;

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
    std::unique_ptr<NiceMock<MockTimerHAL>> hal_timer_owned;
    std::unique_ptr<NiceMock<MockMessageCodec>> codec_owned;
    std::unique_ptr<NiceMock<MockStatisticsManager>> statistics_mgr_owned;
    std::unique_ptr<NiceMock<MockPeerManager>> peer_mgr_owned;

    // Raw pointers to use in tests
    NiceMock<MockTxStateMachine>* fsm;
    NiceMock<MockEspNowHAL>* hal;
    NiceMock<MockTimerHAL>* hal_timer;
    NiceMock<MockMessageCodec>* codec;
    NiceMock<MockStatisticsManager>* statistics_mgr;
    NiceMock<MockPeerManager>* peer_mgr;

    RealFreeRTOSHAL freertos_hal;
    std::unique_ptr<TxManager> manager;

    TxState current_state = TxState::IDLE;
    std::optional<PendingAck> pending_ack = std::nullopt;
    TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(0x6);

    uint32_t ack_timeout_ms = 50;
    uint32_t delay_ms = 20;
    static constexpr uint8_t test_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

    void SetUp() override
    {
        fsm_owned = std::make_unique<NiceMock<MockTxStateMachine>>();
        hal_owned = std::make_unique<NiceMock<MockEspNowHAL>>();
        hal_timer_owned = std::make_unique<NiceMock<MockTimerHAL>>();
        codec_owned = std::make_unique<NiceMock<MockMessageCodec>>();
        statistics_mgr_owned = std::make_unique<NiceMock<MockStatisticsManager>>();
        peer_mgr_owned = std::make_unique<NiceMock<MockPeerManager>>();

        fsm = fsm_owned.get();
        hal = hal_owned.get();
        hal_timer = hal_timer_owned.get();
        codec = codec_owned.get();
        statistics_mgr = statistics_mgr_owned.get();
        peer_mgr = peer_mgr_owned.get();

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

        // PeerManager: resolve test_mac to a known node_id (kNodeId = 2 is defined in test constants)
        ON_CALL(*peer_mgr, find_node_id_by_mac(_, _)).WillByDefault(Invoke([](const uint8_t* mac, NodeId& out_id) {
            static constexpr uint8_t expected_mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
            if (memcmp(mac, expected_mac, 6) == 0) {
                out_id = 2;
                return ESP_OK;
            }
            return ESP_ERR_NOT_FOUND;
        }));

        manager = std::make_unique<TxManager>(
            *fsm_owned, *hal_owned, freertos_hal, *hal_timer_owned, *codec_owned, *statistics_mgr_owned, *peer_mgr_owned);
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
        ASSERT_EQ(ESP_OK, manager->init(4096, 5, fake_rx_task, 50));
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Helper: init with real RX task to avoid segfaults when task_notify is called
    void init_with_real_rx()
    {
        make_real_rx_task();
        ASSERT_EQ(ESP_OK, manager->init(4096, 5, real_rx_task_handle, 50));
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

    // Helper: build a DecodedRxPacket representing a valid logical ACK.
    // sequence_number matches the pending_ack tracked by the fixture (populated via
    // fsm_.set_pending_ack mock's SaveArg). Falls back to 0 if no pending ack exists yet.
    DecodedRxPacket make_ack_packet()
    {
        DecodedRxPacket pkt = {};
        pkt.header.msg_type = MessageType::ACK;
        pkt.header.ack_status = AckStatus::OK;
        pkt.header.sequence_number = pending_ack.has_value() ? pending_ack->sequence_number : 0;
        pkt.raw.timestamp_us = 0;
        return pkt;
    }

    // -------------------------------------------------------------------------
    // Blocking-send helpers
    //
    // queue_packet() blocks the calling task when requires_ack=true because it
    // calls xEventGroupWaitBits() internally. std::thread cannot be used here:
    // POSIX threads lack a FreeRTOS TCB, so xEventGroupWaitBits() aborts with
    // the assertion `uxTopPriority` in vTaskSwitchContext.
    //
    // These helpers spawn a real FreeRTOS task (xTaskCreate) that owns the
    // blocking call. The test thread synchronises via a binary semaphore.
    // -------------------------------------------------------------------------

    struct BlockingSendResult
    {
        esp_err_t result = ESP_FAIL;
        SemaphoreHandle_t done = nullptr;
    };

    // Spawns a FreeRTOS task that calls queue_packet(requires_ack=true) and
    // signals `out.done` when it returns. Call wait_for_blocking_send() after.
    void launch_blocking_send(BlockingSendResult& out)
    {
        out.done = xSemaphoreCreateBinary();
        ASSERT_NE(nullptr, out.done);

        struct Args
        {
            TxManager* mgr;
            DecodedTxPacket pkt;
            BlockingSendResult* out;
        };
        auto* args = new Args{manager.get(), make_packet(true), &out};

        BaseType_t ret = xTaskCreate(
            [](void* raw) {
                auto* a = static_cast<Args*>(raw);
                a->out->result = a->mgr->queue_packet(a->pkt);
                xSemaphoreGive(a->out->done);
                delete a;
                vTaskDelete(nullptr);
            },
            "sender",
            4096,
            args,
            5,
            nullptr);

        ASSERT_EQ(pdPASS, ret) << "Failed to create sender FreeRTOS task";
    }

    // Blocks the test thread until the sender task completes (or timeout_ms elapses).
    // Cleans up the semaphore. Call AFTER all assertions on intermediate state.
    void wait_for_blocking_send(BlockingSendResult& out, uint32_t timeout_ms = 2000)
    {
        ASSERT_EQ(pdTRUE, xSemaphoreTake(out.done, pdMS_TO_TICKS(timeout_ms)))
            << "Sender task did not complete within " << timeout_ms << " ms";
        vSemaphoreDelete(out.done);
        out.done = nullptr;
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

    BlockingSendResult send_result;
    launch_blocking_send(send_result);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);

    manager->handle_ack(make_ack_packet());

    wait_for_blocking_send(send_result);
    EXPECT_EQ(ESP_OK, send_result.result);
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
    EXPECT_CALL(*statistics_mgr, on_delivery_failure(2)).Times(1);

    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, IdleStateNotifyDeliverySuccessCallsFsmOnDeliverySuccess)
{
    init_and_wait();

    EXPECT_CALL(*fsm, on_delivery_success()).Times(1);
    EXPECT_CALL(*statistics_mgr, on_delivery_success(2)).Times(1);

    manager->notify_delivery(ESP_NOW_SEND_SUCCESS, test_mac);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, OnDeliveryFailureReturningTrueCallsNotifyMaxFailuresOnRxTask)
{
    // Create a real rx task defined on the test class
    make_real_rx_task();

    // Init with the real task handle
    ASSERT_EQ(ESP_OK, manager->init(4096, 5, real_rx_task_handle, 50));
    vTaskDelay(pdMS_TO_TICKS(20));

    // On delivery failure returns true if the number of failures is greater than max_failures
    EXPECT_CALL(*fsm, on_delivery_failure()).WillOnce(Return(true));

    // Trigger the notification
    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
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

    EXPECT_CALL(*fsm, on_ack_timeout()).Times(0);  // Should not call on_ack_timeout
    EXPECT_CALL(*fsm, on_ack_received()).Times(1); // Should call on_ack_received

    // 1. Launch blocking send in background
    BlockingSendResult send_result;
    launch_blocking_send(send_result);

    // 2. Wait for tx_task to process the packet and enter WAITING_FOR_ACK
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);

    // 3. Simulate ACK packet arriving via handle_ack()
    manager->handle_ack(make_ack_packet());

    // 4. Wait for background task to complete and assert result
    wait_for_blocking_send(send_result);
    EXPECT_EQ(ESP_OK, send_result.result);
    EXPECT_EQ(TxState::IDLE, current_state);
}

TEST_F(TxManagerTaskTest, WaitingForAckTimeoutCallsOnAckTimeout)
{
    init_and_wait();

    EXPECT_CALL(*fsm, on_ack_timeout()).Times(MAX_FAILURES + 1); // Should call on_ack_timeout for each retry

    BlockingSendResult send_result;
    launch_blocking_send(send_result);

    // Wait for first attempt to enter WAITING_FOR_ACK
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);

    // Wait for all retries and timeouts to exhaust.
    // The total duration is roughly (ack_timeout_ms * 4) + some margin.
    wait_for_blocking_send(send_result, 1000);
    EXPECT_EQ(ESP_ERR_TIMEOUT, send_result.result);
    EXPECT_EQ(TxState::IDLE, current_state);
}

TEST_F(TxManagerTaskTest, WaitingForAckNotifyDeliveryFailureCallsOnDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);

    BlockingSendResult send_result;
    launch_blocking_send(send_result);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);

    // Simulate physical delivery failure (e.g. hub off channel)
    // Note: FSM mock defaults on_delivery_failure to false, meaning it just triggers a retry.
    // To properly simulate MAX_FAILURES, we should override the mock just for this test,
    // or let it retry if the test is just checking the first failure call.
    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);

    // Wait briefly to let the notification be processed
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // To prevent the background task from leaking, inject an ACK to cleanly end it
    manager->handle_ack(make_ack_packet());
    wait_for_blocking_send(send_result);
}

TEST_F(TxManagerTaskTest, WaitingForAckMaxDeliveryFailuresReturnsEspFail)
{
    init_with_real_rx();

    // Override the default mock to return true, simulating MAX_FAILURES reached
    EXPECT_CALL(*fsm, on_delivery_failure()).WillOnce(Return(true));

    BlockingSendResult send_result;
    launch_blocking_send(send_result);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);

    // Simulate physical delivery failure that triggers max failures
    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);

    wait_for_blocking_send(send_result);
    
    // Validate that queue_packet returned ESP_FAIL
    EXPECT_EQ(ESP_FAIL, send_result.result);
    
    // Current state should be IDLE or similar based on FSM resetting, 
    // but the task notify wait terminates the wait.
}

TEST_F(TxManagerTaskTest, WaitingForAckDeinitReturnsInvalidState)
{
    init_and_wait();

    BlockingSendResult send_result;
    launch_blocking_send(send_result);

    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    EXPECT_EQ(TxState::WAITING_FOR_ACK, current_state);

    // Calling deinit() while queue_packet is waiting for ACK should set NOTIFY_TASK_TO_STOP bit
    manager->deinit();

    wait_for_blocking_send(send_result);
    EXPECT_EQ(ESP_ERR_INVALID_STATE, send_result.result);
}

TEST_F(TxManagerTaskTest, DeliveryFailureWithTimeoutSkipsStats)
{
    init_and_wait();

    EXPECT_CALL(*peer_mgr, find_node_id_by_mac(_, _)).WillOnce(Return(ESP_ERR_TIMEOUT));
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);
    EXPECT_CALL(*statistics_mgr, on_delivery_failure(_)).Times(0);

    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, DeliveryFailureWithNotFoundSkipsStats)
{
    init_and_wait();

    EXPECT_CALL(*peer_mgr, find_node_id_by_mac(_, _)).WillOnce(Return(ESP_ERR_NOT_FOUND));
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);
    EXPECT_CALL(*statistics_mgr, on_delivery_failure(_)).Times(0);

    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, DeliverySuccessWithTimeoutSkipsStats)
{
    init_and_wait();

    EXPECT_CALL(*peer_mgr, find_node_id_by_mac(_, _)).WillOnce(Return(ESP_ERR_TIMEOUT));
    EXPECT_CALL(*fsm, on_delivery_success()).Times(1);
    EXPECT_CALL(*statistics_mgr, on_delivery_success(_)).Times(0);

    manager->notify_delivery(ESP_NOW_SEND_SUCCESS, test_mac);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, DeliverySuccessWithNotFoundSkipsStats)
{
    init_and_wait();

    EXPECT_CALL(*peer_mgr, find_node_id_by_mac(_, _)).WillOnce(Return(ESP_ERR_NOT_FOUND));
    EXPECT_CALL(*fsm, on_delivery_success()).Times(1);
    EXPECT_CALL(*statistics_mgr, on_delivery_success(_)).Times(0);

    manager->notify_delivery(ESP_NOW_SEND_SUCCESS, test_mac);
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
    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
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

    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac); // trigger retry loop
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

// =============================================================================
// Statistics Reporting
// =============================================================================

// Delivery success is now reported via callback, not in tx_task loop.
// The on_delivery_success() stats call happens when ESP-NOW callback fires.
// TEST REMOVED: IdleStateProcessesPacketAndReportsSent (no longer applicable)

TEST_F(TxManagerTaskTest, RetryingResendsPacketAndReportsRetry)
{
    init_and_wait();

    // Setup state: RETRYING
    pending_ack = make_pending_ack(2);
    current_state = TxState::RETRYING;

    // Expectation: stats manager is called on retry
    EXPECT_CALL(*statistics_mgr, on_retry(_)).Times(1);

    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
}

TEST_F(TxManagerTaskTest, MaxRetriesExhaustedReportsDeliveryFailure)
{
    init_and_wait();

    // Setup state: RETRYING
    pending_ack = make_pending_ack(0); // No retries left
    current_state = TxState::RETRYING;

    // Expectation: stats manager reports delivery failure
    EXPECT_CALL(*statistics_mgr, on_delivery_failure(_)).Times(1);

    manager->notify_delivery(ESP_NOW_SEND_FAIL, test_mac);
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

    manager->queue_packet(make_packet(false)); // Call with requires_ack == false
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}

TEST_F(TxManagerTaskTest, EspnowNotInitiErrorDoesNotCallDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_NOT_INIT)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);    // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(0); // Should not call on_delivery_failure

    manager->queue_packet(make_packet(false)); // Call with requires_ack == false
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}

TEST_F(TxManagerTaskTest, EspnowArgErrorDoesNotCallDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_ARG)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);                                    // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(0); // Should not call on_delivery_failure

    manager->queue_packet(make_packet(false)); // Call with requires_ack == false
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}

TEST_F(TxManagerTaskTest, EspnowOtherErrorCallsDeliveryFailure)
{
    init_and_wait();

    EXPECT_CALL(*hal, hal_esp_now_send(_, _, _)).WillOnce(Return(ESP_ERR_ESPNOW_CHAN)); // Send fail
    EXPECT_CALL(*fsm, set_pending_ack(_)).Times(0);                                     // Pending ack should not be set
    EXPECT_CALL(*fsm, on_delivery_failure()).Times(1);                                  // Must call on_delivery_failure

    manager->queue_packet(make_packet(false)); // Call with requires_ack == false
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    EXPECT_EQ(TxState::IDLE, current_state); // Should not change state
}
