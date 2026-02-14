#include "unity.h"
#include "espnow_manager.hpp"
#include "esp_log.h"
#include "esp_wifi.h"
extern "C" {
#include "Mockesp_wifi.h"
#include "Mockesp_now.h"
#include "Mockesp_timer.h"
}
#include "nvs_flash.h"
#include <cstring>

/**
 * @file test_espnow_manager_singleton.cpp
 * @brief Smoke tests for the EspNow singleton instance.
 *
 * These tests ensure that the 'instance()' method correctly wires up all real
 * components and that the system can be initialized with a real NVS backend.
 */

static QueueHandle_t app_queue;

static esp_err_t mocked_get_mode(wifi_mode_t* mode, int calls) {
    if (mode) *mode = WIFI_MODE_STA;
    return ESP_OK;
}

void setUp(void) {
    Mockesp_wifi_Init();
    Mockesp_now_Init();
    Mockesp_timer_Init();

    // Default expectations for init()
    esp_wifi_get_mode_StubWithCallback(mocked_get_mode);
    esp_now_init_IgnoreAndReturn(ESP_OK);
    esp_now_register_recv_cb_IgnoreAndReturn(ESP_OK);
    esp_now_register_send_cb_IgnoreAndReturn(ESP_OK);
    esp_wifi_set_channel_IgnoreAndReturn(ESP_OK);
    esp_now_add_peer_IgnoreAndReturn(ESP_OK);
    esp_now_mod_peer_IgnoreAndReturn(ESP_OK);
    esp_now_del_peer_IgnoreAndReturn(ESP_OK);
    esp_now_deinit_IgnoreAndReturn(ESP_OK);
    esp_timer_get_time_IgnoreAndReturn(0);

    // Initialize real NVS for the host environment.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    app_queue = xQueueCreate(10, sizeof(RxPacket));
}

void tearDown(void) {
    // Important: Reset the singleton state if possible, or at least deinit.
    if (EspNow::instance().is_initialized()) {
        EspNow::instance().deinit();
    }
    if (app_queue) vQueueDelete(app_queue);
    nvs_flash_deinit();

    Mockesp_wifi_Verify();
    Mockesp_wifi_Destroy();
    Mockesp_now_Verify();
    Mockesp_now_Destroy();
    Mockesp_timer_Verify();
    Mockesp_timer_Destroy();
}

TEST_CASE("EspNow singleton initialization and basic usage", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    config.node_id = 1;
    config.node_type = 1; // HUB

    // Verify successful initialization of the entire real stack.
    TEST_ASSERT_EQUAL(ESP_OK, EspNow::instance().init(config));

    // Basic API check: sending to a non-existent peer should fail gracefully with NOT_FOUND.
    uint8_t data[] = {1, 2, 3};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, EspNow::instance().send_data(99, 1, data, 3));

    // Verify deinitialization cleans up tasks and resources.
    TEST_ASSERT_EQUAL(ESP_OK, EspNow::instance().deinit());
}
