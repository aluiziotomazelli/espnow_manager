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

    TaskHandle_t rx_handle = nullptr;
    TaskHandle_t worker_handle = nullptr;
    QueueHandle_t rx_queue = nullptr;
    QueueHandle_t worker_queue = nullptr;
    SemaphoreHandle_t ack_mutex = nullptr;

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
        ON_CALL(freertos_hal, task_create(_, _, _, _, _, _)).WillByDefault(Return(pdPASS));
    }

    void TearDown() override
    {
        if (rx_queue) {
            vQueueDelete(rx_queue);
            rx_queue = nullptr;
        }
        if (worker_queue) {
            vQueueDelete(worker_queue);
            worker_queue = nullptr;
        }
        if (ack_mutex) {
            vSemaphoreDelete(ack_mutex);
            ack_mutex = nullptr;
        }
    }

    esp_err_t do_init()
    {
        return bootstrapper->init(config, bootstrap_cfg, rx_queue, worker_queue, ack_mutex, rx_handle, worker_handle);
    }

    esp_err_t do_deinit() { return bootstrapper->deinit(rx_queue, worker_queue, ack_mutex, rx_handle, worker_handle); }
};

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
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _)).Times(0); // Must not call task_create
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenRxTaskCreateFails)
{
    ON_CALL(freertos_hal, task_create(_, _, _, _, _, _)).WillByDefault(Return(pdFAIL));
    ASSERT_EQ(ESP_ERR_NO_MEM, do_init());
}

TEST_F(BootstrapperTest, InitReturnsErrorWhenWorkerTaskCreateFails)
{
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .WillOnce(Return(pdPASS))  // rx_dispatch pass
        .WillOnce(Return(pdFAIL)); // worker fails
    EXPECT_EQ(ESP_ERR_NO_MEM, do_init());
}

TEST_F(BootstrapperTest, DeinitSuccess)
{
    EXPECT_CALL(wifi_hal, hal_esp_now_deinit()).Times(1);
    ASSERT_EQ(ESP_OK, do_deinit());
}

TEST_F(BootstrapperTest, DeinitReturnsErrorWhenEspDeitFails)
{
    EXPECT_CALL(wifi_hal, hal_esp_now_deinit()).Times(1).WillOnce(Return(ESP_FAIL));
    ASSERT_EQ(ESP_FAIL, do_deinit());
}

TEST_F(BootstrapperTest, DeinitDeletesTasksWhenHandlesNotNull)
{
    // task create returns pdPASSS but populates the handle with fake value
    EXPECT_CALL(freertos_hal, task_create(_, _, _, _, _, _))
        .WillOnce([](TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle) {
            *handle = reinterpret_cast<TaskHandle_t>(0x1);
            return pdPASS;
        })
        .WillOnce([](TaskFunction_t, const char *, uint32_t, void *, UBaseType_t, TaskHandle_t *handle) {
            *handle = reinterpret_cast<TaskHandle_t>(0x2);
            return pdPASS;
        });

    ASSERT_EQ(ESP_OK, do_init());

    // Now rx_handle and worker_handle != nullptr
    EXPECT_CALL(freertos_hal, task_delete(reinterpret_cast<TaskHandle_t>(0x1))).Times(1);
    EXPECT_CALL(freertos_hal, task_delete(reinterpret_cast<TaskHandle_t>(0x2))).Times(1);
    EXPECT_CALL(wifi_hal, hal_esp_now_deinit()).WillOnce(Return(ESP_OK));

    bootstrapper->deinit(rx_queue, worker_queue, ack_mutex, rx_handle, worker_handle);
}