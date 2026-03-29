#include "gmock/gmock.h"
#include "gtest/gtest.h"

#include "mock_tx_state_machine.hpp"
#include "mock_hal_espnow.hpp"
#include "mock_message_codec.hpp"
#include "mock_hal_freertos.hpp"
#include "tx_manager.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::SetArgPointee;

class TxManagerTest : public ::testing::Test
{
protected:
    NiceMock<MockTxStateMachine> fsm;
    NiceMock<MockEspNowHAL> hal;
    NiceMock<MockFreeRTOSHAL> freertos_hal;
    NiceMock<MockMessageCodec> codec;
    std::unique_ptr<TxManager> manager;

    // Fake handles
    QueueHandle_t fake_queue = reinterpret_cast<QueueHandle_t>(0x1);
    TimerHandle_t fake_timer = reinterpret_cast<TimerHandle_t>(0x2);
    SemaphoreHandle_t fake_semaphore = reinterpret_cast<SemaphoreHandle_t>(0x3);
    TaskHandle_t fake_task = reinterpret_cast<TaskHandle_t>(0x4);
    SemaphoreHandle_t fake_mutex = reinterpret_cast<SemaphoreHandle_t>(0x5);
    TaskHandle_t fake_rx_task = reinterpret_cast<TaskHandle_t>(0x6);

    void SetUp() override
    {
        // init() happy path
        ON_CALL(freertos_hal, queue_create(_, _)).WillByDefault(Return(fake_queue));
        ON_CALL(freertos_hal, semaphore_create_binary()).WillByDefault(Return(fake_semaphore));
        ON_CALL(freertos_hal, mutex_create()).WillByDefault(Return(fake_mutex));
        ON_CALL(freertos_hal, timer_create(_, _, _, _, _)).WillByDefault(Return(fake_timer));
        ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
            .WillByDefault(DoAll(SetArgPointee<5>(fake_task), Return(pdPASS)));

        // deinit() happy path
        ON_CALL(freertos_hal, task_notify(_, NOTIFY_STOP, _)).WillByDefault(Return(pdPASS));
        ON_CALL(freertos_hal, queue_send(_, _, _)).WillByDefault(Return(pdTRUE));
        ON_CALL(freertos_hal, semaphore_take(_, _)).WillByDefault(Return(pdPASS));
        ON_CALL(freertos_hal, task_delete(_)).WillByDefault(Return());
        ON_CALL(freertos_hal, semaphore_delete(_)).WillByDefault(Return());

        ON_CALL(freertos_hal, queue_delete(_)).WillByDefault(Return());
        ON_CALL(freertos_hal, timer_delete(_, _)).WillByDefault(Return(pdPASS));

        manager = std::make_unique<TxManager>(fsm, hal, freertos_hal, codec, 10);
    }

    void deinit_after_init()
    {
        EXPECT_CALL(freertos_hal, task_notify(_, NOTIFY_STOP, _)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, queue_send(fake_queue, _, _)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, semaphore_take(fake_semaphore, _)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, task_delete(fake_task)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, semaphore_delete(fake_semaphore)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, queue_delete(fake_queue)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, timer_delete(fake_timer, _)).Times(AnyNumber());
        EXPECT_CALL(freertos_hal, semaphore_delete(fake_mutex)).Times(AnyNumber());

        manager->deinit();
    }
};

// ===================================================
// Init
// ===================================================

TEST_F(TxManagerTest, InitSetsTaskHandle)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));
    ASSERT_EQ(fake_task, manager->get_task_handle());
    deinit_after_init();
}

TEST_F(TxManagerTest, InitCallsAllCreateFunctions)
{
    EXPECT_CALL(freertos_hal, queue_create(_, sizeof(DecodedTxPacket))).Times(1);
    EXPECT_CALL(freertos_hal, semaphore_create_binary()).Times(1);
    EXPECT_CALL(freertos_hal, timer_create(_, _, _, _, _)).Times(1);
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).Times(1);
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));
}

TEST_F(TxManagerTest, InitWithoutRxTaskHandleFails)
{
    EXPECT_EQ(ESP_ERR_INVALID_ARG, manager->init(1000, 1, nullptr));
}

TEST_F(TxManagerTest, InitQueueCreationFailsReturnsError)
{
    EXPECT_CALL(freertos_hal, queue_create(_, _)).Times(1).WillOnce(Return(nullptr));
    EXPECT_EQ(ESP_ERR_NO_MEM, manager->init(1000, 1, fake_rx_task));
}

TEST_F(TxManagerTest, InitSemaphoreCreationFailsReturnsErrorAndCallsDeinit)
{
    EXPECT_CALL(freertos_hal, semaphore_create_binary()).Times(1).WillOnce(Return(nullptr)); // Simulate error
    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(1); // Must be called on deinit()

    EXPECT_EQ(ESP_ERR_NO_MEM, manager->init(1000, 1, fake_rx_task));
}

TEST_F(TxManagerTest, InitTimerCreationFailsReturnsError)
{
    EXPECT_CALL(freertos_hal, timer_create(_, _, _, _, _)).Times(1).WillOnce(Return(nullptr)); // Simulate error
    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(1); // Must be called on deinit()

    EXPECT_EQ(ESP_ERR_NO_MEM, manager->init(1000, 1, fake_rx_task));
}

TEST_F(TxManagerTest, InitTaskCreationFailsReturnsError)
{
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).Times(1).WillOnce(Return(pdFAIL)); // Simulate error
    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(1); // Must be called on deinit()

    EXPECT_EQ(ESP_ERR_NO_MEM, manager->init(1000, 1, fake_rx_task));
}

// ===================================================
// Deinit
// ===================================================

TEST_F(TxManagerTest, DeinitCallsAllDeleteFunctions)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));

    EXPECT_CALL(freertos_hal, task_notify(fake_task, _, _)).Times(1);
    EXPECT_CALL(freertos_hal, queue_send(_, _, _)).Times(1);
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).Times(1);
    EXPECT_CALL(freertos_hal, task_delete(fake_task)).Times(1);
    EXPECT_CALL(freertos_hal, semaphore_delete(_)).Times(1);
    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(1);
    EXPECT_CALL(freertos_hal, timer_delete(_, _)).Times(1);

    manager->deinit();
}

TEST_F(TxManagerTest, DeinitWithoutTaskHandleDoesNotTryToDeleteTask)
{
    ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<5>(nullptr), Return(pdPASS))); // Simulate no task handle
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));

    // With task_handle the deinit
    EXPECT_CALL(freertos_hal, task_notify(_, _, _)).Times(0); // Does not try to notify taks
    EXPECT_CALL(freertos_hal, queue_send(_, _, _)).Times(0);  // Does not try to send packet to weakup task
    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).Times(0); // Does not try to take semaphore
    EXPECT_CALL(freertos_hal, task_delete(_)).Times(0);       // Does not try to delete task
    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(1);      // But still tries to delete queue

    manager->deinit();
}

TEST_F(TxManagerTest, FailToTakeSemaphoreStillDeletesTask)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));

    EXPECT_CALL(freertos_hal, semaphore_take(_, _)).WillRepeatedly(Return(pdFAIL)); // Simulate semaphore take failure
    EXPECT_CALL(freertos_hal, task_delete(_)).Times(1);                             // But still tries to delete task

    manager->deinit();
}

// ===================================================
// TxManager::notify_*()
// ===================================================

TEST_F(TxManagerTest, NotifyPhysicalFailCallsTaskNotify)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));
    EXPECT_CALL(freertos_hal, task_notify(fake_task, NOTIFY_PHYSICAL_FAIL, _)).Times(1);
    manager->notify_physical_fail();
    deinit_after_init();
}

TEST_F(TxManagerTest, NotifyLinKAliveCallsTaskNotify)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));
    EXPECT_CALL(freertos_hal, task_notify(fake_task, NOTIFY_LINK_ALIVE, _)).Times(1);
    manager->notify_link_alive();
}

TEST_F(TxManagerTest, NotifyLogicalAckCallsTaskNotify)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));
    EXPECT_CALL(freertos_hal, task_notify(fake_task, NOTIFY_LOGICAL_ACK, _)).Times(1);
    manager->notify_logical_ack();
}

TEST_F(TxManagerTest, NotifyWithoutTaskHandleDoesNotCallTaskNotify)
{
    ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<5>(nullptr), Return(pdPASS))); // Simulate no task handle
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));

    EXPECT_CALL(freertos_hal, task_notify(_, _, _)).Times(0);

    manager->notify_physical_fail();
    manager->notify_logical_ack();
    manager->notify_link_alive();
}

// ===================================================
// TxManager::queue_packet(const DecodedTxPacket &packet)
// ===================================================

TEST_F(TxManagerTest, QueuePacketWithoutQueueReturnsError)
{
    // Calling without init, the queue handle tx_queue_ == nullptr
    DecodedTxPacket packet = {};
    EXPECT_EQ(ESP_ERR_INVALID_STATE, manager->queue_packet(packet)); // Must return error
}

TEST_F(TxManagerTest, QueuePacketCallsQueueSend)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task)); // Initialize first
    DecodedTxPacket packet = {};                             // Queue packet
    EXPECT_CALL(freertos_hal, queue_send(_, _, _)).Times(1); // Must call queue send
    EXPECT_EQ(ESP_OK, manager->queue_packet(packet));        // Queue packet
}

TEST_F(TxManagerTest, QueuePacketWithoutTaskHandleDoesNotTryToNotifyTask)
{
    ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .WillByDefault(DoAll(SetArgPointee<5>(nullptr), Return(pdPASS))); // Simulate no task handle
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));

    DecodedTxPacket packet = {};
    EXPECT_CALL(freertos_hal, task_notify(_, _, _)).Times(0); // Must not try to notify task
    EXPECT_EQ(ESP_OK, manager->queue_packet(packet));         // Queue packet
}

TEST_F(TxManagerTest, QueueSendFailOnQueuePacketReturnsError)
{
    EXPECT_EQ(ESP_OK, manager->init(1000, 1, fake_rx_task));
    DecodedTxPacket packet = {};
    ON_CALL(freertos_hal, queue_send(_, _, _)).WillByDefault(Return(pdFAIL)); // Simulate queue send failure
    EXPECT_EQ(ESP_FAIL, manager->queue_packet(packet));                       // Must return error
}
