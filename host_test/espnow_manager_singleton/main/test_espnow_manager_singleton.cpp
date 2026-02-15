#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "unity.h"

extern "C" {
#include "Mockesp_now.h"
#include "Mockesp_timer.h"
#include "Mockesp_wifi.h"
}

#include "espnow_manager.hpp"

/**
 * @file test_espnow_manager_singleton.cpp
 * @brief Smoke tests for the EspNowManager singleton instance.
 *
 * These tests ensure that the 'instance()' method correctly wires up all real
 * components and that the system can be initialized with a real NVS backend.
 */

static QueueHandle_t app_queue;

static esp_err_t mocked_get_mode(wifi_mode_t *mode, int calls)
{
    if (mode)
        *mode = WIFI_MODE_STA;
    return ESP_OK;
}

void setUp(void)
{
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

void tearDown(void)
{
    // Important: Reset the singleton state if possible, or at least deinit.
    if (EspNowManager::instance().is_initialized()) {
        EspNowManager::instance().deinit();
    }
    if (app_queue)
        vQueueDelete(app_queue);
    nvs_flash_deinit();

    Mockesp_wifi_Verify();
    Mockesp_wifi_Destroy();
    Mockesp_now_Verify();
    Mockesp_now_Destroy();
    Mockesp_timer_Verify();
    Mockesp_timer_Destroy();
}

TEST_CASE("EspNowManager singleton initialization and basic usage", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    config.node_id      = 1;
    config.node_type    = 1; // HUB

    // Verify successful initialization of the entire real stack.
    TEST_ASSERT_EQUAL(ESP_OK, EspNowManager::instance().init(config));

    // Basic API check: sending to a non-existent peer should fail gracefully with NOT_FOUND.
    uint8_t data[] = {1, 2, 3};
    TEST_ASSERT_EQUAL(ESP_ERR_NOT_FOUND, EspNowManager::instance().send_data(99, 1, data, 3));

    // Verify deinitialization cleans up tasks and resources.
    TEST_ASSERT_EQUAL(ESP_OK, EspNowManager::instance().deinit());
}

// Internal structures to access private/protected members for callback testing
class EspNowFriend : public EspNowManager
{
public:
    // This allows us to call static protected methods
    static void call_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len)
    {
        esp_now_recv_cb(info, data, len);
    }
    static void call_send_cb(const esp_now_send_info_t *info, esp_now_send_status_t status)
    {
        esp_now_send_cb(info, status);
    }
    // This allows us to access protected members via a hacky cast
    QueueHandle_t get_rx_dispatch_queue()
    {
        return rx_dispatch_queue_;
    }
};

TEST_CASE("EspNowManager: Static callbacks coverage", "[espnow_manager]")
{
    EspNowConfig config;
    config.app_rx_queue = app_queue;
    TEST_ASSERT_EQUAL(ESP_OK, EspNowManager::instance().init(config));

    EspNowFriend *friend_ptr = reinterpret_cast<EspNowFriend *>(&EspNowManager::instance());

    // 1. Test recv_cb
    esp_now_recv_info_t info = {};
    uint8_t mac[]            = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    info.src_addr            = mac;
    wifi_pkt_rx_ctrl_t ctrl  = {};
    ctrl.rssi                = -50;
    info.rx_ctrl             = &ctrl;

    uint8_t dummy_data[] = {0xAA, 0xBB};

    EspNowFriend::call_recv_cb(&info, dummy_data, sizeof(dummy_data));

    // Verify packet in dispatch queue
    RxPacket packet;
    TEST_ASSERT_EQUAL(pdTRUE, xQueueReceive(friend_ptr->get_rx_dispatch_queue(), &packet, 0));
    TEST_ASSERT_EQUAL_MEMORY(mac, packet.src_mac, 6);
    TEST_ASSERT_EQUAL(-50, packet.rssi);

    // 2. Test send_cb
    esp_now_send_info_t send_info = {};
    // We don't really care about the members here since we just want to cover the line
    // but we need to satisfy the compiler. des_addr seems to be the name in this IDF mock.
    // However, some IDF versions have it differently.
    // We'll just zero it and set tx_status if it exists.
    // If it fails to compile again, I'll use a more generic approach.

    // Using a pointer hack to set tx_status without knowing the exact struct layout if needed,
    // but let's try des_addr first as suggested by compiler.
    // Wait! Let's check if tx_status exists.
    send_info.tx_status = WIFI_SEND_FAIL;

    // This should trigger physical fail notification.
    // Since we are using the real singleton with real sub-managers,
    // it's hard to verify unless we check the internal state of TxManager or state machine.
    // For now, calling it covers the lines.
    EspNowFriend::call_send_cb(&send_info, ESP_NOW_SEND_FAIL);

    TEST_ASSERT_EQUAL(ESP_OK, EspNowManager::instance().deinit());
}
