#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "bootstrapper.hpp"
#include "mock_hal_wifi.hpp"
#include "mock_hal_freertos.hpp"

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

// Minimal task stub used as the TaskFunction_t argument in init() calls.
// The function is never executed during tests because task_create is mocked;
// it only exists to satisfy the function-pointer parameter type.
static void dummy_rx_task(void *)
{
    vTaskDelete(nullptr);
}
static void dummy_worker_task(void *)
{
    vTaskDelete(nullptr);
}

class BootstrapperTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> wifi_hal;
    NiceMock<MockFreeRTOSHAL> freertos_hal;
    std::unique_ptr<Bootstrapper> bootstrapper;

    // Real handles
    TaskHandle_t rx_handle = nullptr;
    TaskHandle_t worker_handle = nullptr;
    QueueHandle_t rx_queue = nullptr;
    QueueHandle_t worker_queue = nullptr;
    SemaphoreHandle_t ack_mutex = nullptr;

    // Fake handles == nullptr
    TaskHandle_t fake_rx_handle = reinterpret_cast<TaskHandle_t>(0x1);
    TaskHandle_t fake_worker_handle = reinterpret_cast<TaskHandle_t>(0x2);
    QueueHandle_t fake_rx_queue = reinterpret_cast<QueueHandle_t>(0x3);
    QueueHandle_t fake_worker_queue = reinterpret_cast<QueueHandle_t>(0x4);
    SemaphoreHandle_t fake_ack_mutex = reinterpret_cast<SemaphoreHandle_t>(0x5);

    EspNowConfig config;
    EspNowBootstrapConfig bootstrap_cfg;

    void SetUp() override
    {
        bootstrapper = std::make_unique<Bootstrapper>(wifi_hal, freertos_hal);

        bootstrap_cfg.recv_cb = nullptr;
        bootstrap_cfg.send_cb = nullptr;
        bootstrap_cfg.rx_dispatch_fn = dummy_rx_task;
        bootstrap_cfg.transport_worker_fn = dummy_worker_task;
        bootstrap_cfg.task_params = nullptr;

        // Default happy path
        ON_CALL(wifi_hal, wifi_get_mode(_)).WillByDefault([](wifi_mode_t *mode) {
            *mode = WIFI_MODE_STA;
            return ESP_OK;
        });
        ON_CALL(wifi_hal, hal_esp_now_init()).WillByDefault(Return(ESP_OK));
        ON_CALL(wifi_hal, hal_espnow_register_recv_cb(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(wifi_hal, hal_espnow_register_send_cb(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(wifi_hal, wifi_set_channel(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(wifi_hal, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_OK));

        ON_CALL(freertos_hal, mutex_create()).WillByDefault(Return(fake_ack_mutex));
        ON_CALL(freertos_hal, queue_create(_, _)).WillByDefault(Return(fake_rx_queue));
        ON_CALL(freertos_hal, task_create(_, _, _, _, _, _))
            .WillByDefault([this](TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle) {
                *handle = fake_rx_handle;
                return pdPASS;
            });
    }

    void TearDown() override
    {
        // Empty by now
    }

    esp_err_t do_init()
    {
        return bootstrapper->init(config, bootstrap_cfg, rx_queue, worker_queue, ack_mutex, rx_handle, worker_handle);
    }

    esp_err_t do_deinit() { return bootstrapper->deinit(rx_queue, worker_queue, ack_mutex, rx_handle, worker_handle); }
};

// =========================================================================
// Init
// =========================================================================

TEST_F(BootstrapperTest, InitFailGetWifiMode)
{
    ON_CALL(wifi_hal, wifi_get_mode(_)).WillByDefault(Return(ESP_ERR_INVALID_STATE)); // Returns ESP_ERR_INVALID_STATE
    EXPECT_CALL(wifi_hal, hal_esp_now_init()).Times(0); // Must not call esp_now_init, return imediately
    ASSERT_EQ(ESP_ERR_INVALID_STATE, do_init());
}

TEST_F(BootstrapperTest, InitGetWrongMode)
{
    ON_CALL(wifi_hal, wifi_get_mode(_)).WillByDefault([](wifi_mode_t *mode) {
        *mode = WIFI_MODE_NULL;
        return ESP_OK;
    });
    EXPECT_CALL(wifi_hal, hal_esp_now_init()).Times(0); // Must not call esp_now_init, return imediately
    ASSERT_EQ(ESP_ERR_INVALID_STATE, do_init());
}

TEST_F(BootstrapperTest, HalEspnowInitFails)
{
    ON_CALL(wifi_hal, hal_esp_now_init()).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(wifi_hal, hal_espnow_register_recv_cb(_)).Times(0); // Must not call hal_espnow_register_recv_cb
    EXPECT_CALL(wifi_hal, hal_espnow_register_send_cb(_)).Times(0);
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(BootstrapperTest, HalEspnowRegisterRecvCbFails)
{
    ON_CALL(wifi_hal, hal_espnow_register_recv_cb(_)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(wifi_hal, hal_espnow_register_send_cb(_)).Times(0); // Must not call hal_espnow_register_send_cb
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(BootstrapperTest, HalEspnowRegisterSendCbFails)
{
    ON_CALL(wifi_hal, hal_espnow_register_send_cb(_)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(wifi_hal, wifi_set_channel(_, _)).Times(0); // Must not call wifi_set_channel
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(BootstrapperTest, HalWifiSetChannelFails)
{
    ON_CALL(wifi_hal, wifi_set_channel(_, _)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(0); // Must not call esp_now_add_peer
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(BootstrapperTest, HalEspNowAddPeerFails)
{
    ON_CALL(wifi_hal, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(freertos_hal, mutex_create()).Times(0); // Must not call mutex_create
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenMutexCreateFails)
{
    ON_CALL(freertos_hal, mutex_create()).WillByDefault(Return(nullptr));
    EXPECT_CALL(freertos_hal, queue_create(_, _)).Times(0); // Must not call queue_create
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init());
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenQueueCreateFails)
{
    EXPECT_CALL(freertos_hal, queue_create(_, _)).WillOnce(Return(nullptr));
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).Times(0); // Must not call task_create
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init());
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenSecondQueueCreateFails)
{
    EXPECT_CALL(freertos_hal, queue_create(_, _))
        .Times(2)
        .WillOnce(Return(fake_rx_queue))                               // First call returns fake_rx_queue
        .WillOnce(Return(nullptr));                                    // Second call returns nullptr
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).Times(0); // Must not call task_create
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init());
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenRxTaskCreateFails)
{
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .Times(1)                         // On first task
        .WillOnce(Return(pdFAIL));        // rx_dispatch fails
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init()); // Returns error
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenWorkerTaskCreateFails)
{
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .Times(2)
        .WillOnce(Return(pdPASS))  // rx_dispatch pass
        .WillOnce(Return(pdFAIL)); // worker fails
    EXPECT_EQ(ESP_ERR_NO_MEM, do_init());
}

TEST_F(BootstrapperTest, InitCompletesSuccessfully)
{
    EXPECT_CALL(wifi_hal, wifi_get_mode(_)).Times(1);
    EXPECT_CALL(wifi_hal, hal_esp_now_init()).Times(1);
    EXPECT_CALL(wifi_hal, hal_espnow_register_recv_cb(_)).Times(1);
    EXPECT_CALL(wifi_hal, hal_espnow_register_send_cb(_)).Times(1);
    EXPECT_CALL(wifi_hal, wifi_set_channel(_, _)).Times(1);
    EXPECT_CALL(wifi_hal, hal_esp_now_add_peer(_)).Times(1);
    EXPECT_CALL(freertos_hal, mutex_create()).Times(1);
    EXPECT_CALL(freertos_hal, queue_create(_, _)).Times(2);
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).Times(2);

    EXPECT_EQ(ESP_OK, do_init());
}

// =========================================================================
// Deinit
// =========================================================================

TEST_F(BootstrapperTest, DeinitSuccess)
{
    EXPECT_EQ(ESP_OK, do_init());

    EXPECT_CALL(freertos_hal, task_delete(_)).Times(2);
    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(2);
    EXPECT_CALL(freertos_hal, semaphore_delete(_)).Times(1);
    EXPECT_CALL(wifi_hal, hal_esp_now_deinit()).Times(1);

    ASSERT_EQ(ESP_OK, do_deinit());
}

TEST_F(BootstrapperTest, DeinitReturnsErrorWhenEspDeitFails)
{
    EXPECT_CALL(wifi_hal, hal_esp_now_deinit()).Times(1).WillOnce(Return(ESP_FAIL));
    ASSERT_EQ(ESP_FAIL, do_deinit());
}

TEST_F(BootstrapperTest, DeinitDoesNotDeleteTasksWhenHandlesNull)
{
    // Create tasks without handles
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).WillRepeatedly(Return(pdPASS));
    ASSERT_EQ(ESP_OK, do_init());

    EXPECT_CALL(freertos_hal, task_delete(_)).Times(0); // Tasks will not be deleted

    EXPECT_EQ(ESP_OK, do_deinit()); // But deinit does not fail
}

TEST_F(BootstrapperTest, DeinitDeletesTasksWhenHandlesNotNull)
{
    // Create tasks with distinct handles
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .WillOnce([this](TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle) {
            *handle = fake_rx_handle;
            return pdPASS;
        })
        .WillOnce([this](TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle) {
            *handle = fake_worker_handle;
            return pdPASS;
        });

    ASSERT_EQ(ESP_OK, do_init());

    // Delete tasks
    EXPECT_CALL(freertos_hal, task_delete(fake_rx_handle)).Times(1);
    EXPECT_CALL(freertos_hal, task_delete(fake_worker_handle)).Times(1);
    EXPECT_CALL(wifi_hal, hal_esp_now_deinit()).WillOnce(Return(ESP_OK));

    bootstrapper->deinit(rx_queue, worker_queue, ack_mutex, rx_handle, worker_handle);
}

TEST_F(BootstrapperTest, DeinitDoesNotDeleteQueuesWhenHandlesNull)
{
    // With nullptr handles
    ON_CALL(freertos_hal, queue_create(_, _)).WillByDefault(Return(nullptr));
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init()); // Init fails

    EXPECT_CALL(freertos_hal, queue_delete(_)).Times(0); // Queues should not be deleted
    EXPECT_EQ(ESP_OK, do_deinit());                      // But deinit does not fail
}

TEST_F(BootstrapperTest, DeinitDoesNotDeleteMutexWhenHandleNull)
{
    // With nullptr handle
    ON_CALL(freertos_hal, mutex_create()).WillByDefault(Return(nullptr));
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init()); // Init fails

    EXPECT_CALL(freertos_hal, semaphore_delete(_)).Times(0); // Mutex should not be deleted
    EXPECT_EQ(ESP_OK, do_deinit());                          // But deinit does not fail
}