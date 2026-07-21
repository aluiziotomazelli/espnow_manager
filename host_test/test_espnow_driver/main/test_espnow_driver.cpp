#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "espnow_driver.hpp"
#include "mock_en_hal_wifi.hpp"
#include "mock_en_hal_espnow.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
using namespace espnow;

using ::testing::_;
using ::testing::NiceMock;
using ::testing::Return;

class EspNowDriverTest : public ::testing::Test
{
protected:
    NiceMock<MockWiFiHAL> hal_wifi;
    NiceMock<MockEspNowHAL> hal_espnow;
    std::unique_ptr<EspNowDriver> espnow_driver;

    EspNowConfig config;
    esp_now_recv_cb_t recv_cb = nullptr;
    esp_now_send_cb_t send_cb = nullptr;

    void SetUp() override
    {
        espnow_driver = std::make_unique<EspNowDriver>(hal_wifi, hal_espnow);

        // Default happy path
        ON_CALL(hal_wifi, wifi_get_mode(_)).WillByDefault([](wifi_mode_t* mode) {
            *mode = WIFI_MODE_STA;
            return ESP_OK;
        });
        ON_CALL(hal_wifi, wifi_set_channel(_, _)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_espnow, hal_esp_now_init()).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_espnow, hal_espnow_register_recv_cb(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_espnow, hal_espnow_register_send_cb(_)).WillByDefault(Return(ESP_OK));
        ON_CALL(hal_espnow, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_OK));
    }

    void TearDown() override
    {
        // Empty by now
    }

    esp_err_t do_init() { return espnow_driver->init(config, recv_cb, send_cb); }

    esp_err_t do_deinit() { return espnow_driver->deinit(); }
};

// =========================================================================
// Init
// =========================================================================

TEST_F(EspNowDriverTest, InitFailGetWifiMode)
{
    ON_CALL(hal_wifi, wifi_get_mode(_)).WillByDefault(Return(ESP_ERR_INVALID_STATE)); // Returns ESP_ERR_INVALID_STATE
    EXPECT_CALL(hal_espnow, hal_esp_now_init()).Times(0); // Must not call esp_now_init, return imediately
    ASSERT_EQ(ESP_ERR_INVALID_STATE, do_init());
}

TEST_F(EspNowDriverTest, InitGetWrongMode)
{
    ON_CALL(hal_wifi, wifi_get_mode(_)).WillByDefault([](wifi_mode_t* mode) {
        *mode = WIFI_MODE_NULL;
        return ESP_OK;
    });
    EXPECT_CALL(hal_espnow, hal_esp_now_init()).Times(0); // Must not call esp_now_init, return imediately
    ASSERT_EQ(ESP_ERR_INVALID_STATE, do_init());
}

TEST_F(EspNowDriverTest, HalEspnowInitFails)
{
    ON_CALL(hal_espnow, hal_esp_now_init()).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(hal_espnow, hal_espnow_register_recv_cb(_)).Times(0); // Must not call hal_espnow_register_recv_cb
    EXPECT_CALL(hal_espnow, hal_espnow_register_send_cb(_)).Times(0);
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(EspNowDriverTest, HalEspnowRegisterRecvCbFails)
{
    ON_CALL(hal_espnow, hal_espnow_register_recv_cb(_)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(hal_espnow, hal_espnow_register_send_cb(_)).Times(0); // Must not call hal_espnow_register_send_cb
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(EspNowDriverTest, HalEspnowRegisterSendCbFails)
{
    ON_CALL(hal_espnow, hal_espnow_register_send_cb(_)).WillByDefault(Return(ESP_FAIL));
    EXPECT_CALL(hal_wifi, wifi_set_channel(_, _)).Times(0); // Must not call wifi_set_channel
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(EspNowDriverTest, HalEspNowAddPeerFails)
{
    ON_CALL(hal_espnow, hal_esp_now_add_peer(_)).WillByDefault(Return(ESP_FAIL));
    ASSERT_EQ(ESP_FAIL, do_init());
}

TEST_F(EspNowDriverTest, InitCompletesSuccessfully)
{
    EXPECT_CALL(hal_wifi, wifi_get_mode(_)).Times(1);
    EXPECT_CALL(hal_espnow, hal_esp_now_init()).Times(1);
    EXPECT_CALL(hal_espnow, hal_espnow_register_recv_cb(_)).Times(1);
    EXPECT_CALL(hal_espnow, hal_espnow_register_send_cb(_)).Times(1);
    EXPECT_CALL(hal_espnow, hal_esp_now_add_peer(_)).Times(1);

    EXPECT_EQ(ESP_OK, do_init());
}

// =========================================================================
// Deinit
// =========================================================================

TEST_F(EspNowDriverTest, DeinitSuccess)
{
    EXPECT_EQ(ESP_OK, do_init());

    EXPECT_CALL(hal_espnow, hal_esp_now_deinit()).Times(1);

    ASSERT_EQ(ESP_OK, do_deinit());
}

TEST_F(EspNowDriverTest, DeinitReturnsErrorWhenEspDeitFails)
{
    EXPECT_CALL(hal_espnow, hal_esp_now_deinit()).Times(1).WillOnce(Return(ESP_FAIL));
    ASSERT_EQ(ESP_FAIL, do_deinit());
}
