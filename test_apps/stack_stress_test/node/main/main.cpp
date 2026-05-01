#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "espnow_manager.hpp"
#include "test_config.hpp"

using namespace espnow;
using namespace stack_test;

static const char* TAG = "STRESS_NODE";

static void wifi_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
}

static void print_stack_watermarks()
{
    TaskHandle_t rx = xTaskGetHandle("rx_task");
    TaskHandle_t tx = xTaskGetHandle("tx_task");
    TaskHandle_t disc = xTaskGetHandle("discovery_task");

    ESP_LOGI(TAG, "--- Stack High Water Mark ---");
    if (rx)
        ESP_LOGI(TAG, "RX Task: %lu bytes", (unsigned long)uxTaskGetStackHighWaterMark(rx));
    if (tx)
        ESP_LOGI(TAG, "TX Task: %lu bytes", (unsigned long)uxTaskGetStackHighWaterMark(tx));
    if (disc)
        ESP_LOGI(TAG, "Disc Task: %lu bytes", (unsigned long)uxTaskGetStackHighWaterMark(disc));
    ESP_LOGI(TAG, "-----------------------------");
}

extern "C" void app_main(void)
{
    ESP_LOGW(TAG, "Stack Stress Test Node: Erasing NVS for clean start...");
    nvs_flash_erase();

    wifi_init();

    QueueHandle_t app_queue = xQueueCreate(20, sizeof(stack_test::AppMessage));

    EspNowConfig config;
    config.node_id = NODE_ID;
    config.node_type = NODE_TYPE;
    config.wifi_channel = 1; // Start at 1
    config.app_rx_queue = app_queue;
    config.heartbeat_interval_ms = HEARTBEAT_INTERVAL_MS;

    EspNowManager& manager = EspNowManager::instance();
    esp_err_t err = manager.init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Starting pairing for 60s...");
    manager.start_pairing(60000);

    uint32_t seq = 0;
    stack_test::AppMessage msg = {};
    msg.requires_ack = true;
    msg.sender_id = config.node_id;

    while (true) {
        NodeState state = manager.get_node_state();
        if (state == NodeState::OPERATIONAL) {
            msg.sequence_number = seq++;
            err = manager.send_data(ReservedIds::HUB, stack_test::TEST_PAYLOAD_TYPE, &msg, sizeof(msg), true);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Sent msg seq=%lu to HUB", (unsigned long)msg.sequence_number);
            }
            else {
                ESP_LOGW(TAG, "Failed to send msg: %s", esp_err_to_name(err));
            }

            PeerStatistics stats;
            if (manager.get_peer_stats(ReservedIds::HUB, stats)) {
                ESP_LOGI(
                    TAG,
                    "HUB Stats: RX: %lu | S: %lu | L: %lu",
                    (unsigned long)stats.packets_rx,
                    (unsigned long)stats.packets_sent,
                    (unsigned long)stats.packets_lost);
            }
        }
        else {
            ESP_LOGI(TAG, "Waiting for Hub connection... Current State: %d", (int)state);
        }

        print_stack_watermarks();

        vTaskDelay(pdMS_TO_TICKS(SEND_INTERVAL_MS));
    }
}
