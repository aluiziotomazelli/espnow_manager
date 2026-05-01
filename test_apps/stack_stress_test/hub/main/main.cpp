#include <stdio.h>
#include <stdlib.h>
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

static const char* TAG = "STRESS_HUB";

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
    ESP_ERROR_CHECK(esp_wifi_set_channel(13, WIFI_SECOND_CHAN_NONE));
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
    ESP_LOGW(TAG, "Stack Stress Test Hub: Erasing NVS for clean start...");
    nvs_flash_erase();

    wifi_init();

    QueueHandle_t app_queue = xQueueCreate(20, sizeof(stack_test::AppMessage));

    EspNowConfig config;
    config.node_id = ReservedIds::HUB;
    config.node_type = ReservedTypes::HUB;
    config.wifi_channel = 13;
    config.app_rx_queue = app_queue;
    config.heartbeat_interval_ms = stack_test::HEARTBEAT_INTERVAL_MS;
    config.channel_monitor_interval_ms = stack_test::CHANNEL_MONITOR_INTERVAL_MS;

    EspNowManager& manager = EspNowManager::instance();
    esp_err_t err = manager.init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Entering pairing mode for 60s...");
    manager.start_pairing(60000);

    ESP_LOGI(TAG, "HUB initialized on Channel 13. Waiting for messages...");

    stack_test::AppMessage msg;
    uint32_t message_count = 0;
    uint8_t current_channel = 13;

    while (true) {
        // Wait for incoming messages
        if (xQueueReceive(app_queue, &msg, portMAX_DELAY) == pdTRUE) {
            if (msg.requires_ack) {
                esp_err_t ack_err = manager.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
                if (ack_err == ESP_ERR_NOT_FOUND) {
                    ESP_LOGW(TAG, "Received message from unregistered Node %d. Please re-pair.", msg.sender_id);
                }
            }

            message_count++;

            PeerStatistics stats;
            if (manager.get_peer_stats(stack_test::NODE_ID, stats)) {
                ESP_LOGI(
                    TAG,
                    "Node %d Stats: RX: %lu | S: %lu | L: %lu",
                    stack_test::NODE_ID,
                    (unsigned long)stats.packets_rx,
                    (unsigned long)stats.packets_sent,
                    (unsigned long)stats.packets_lost);
            }

            // Print memory status after ACK is sent
            print_stack_watermarks();

            // Channel hop logic: change channel every 10 messages
            if (message_count % 20 == 0) {
                current_channel++;
                if (current_channel > 13) {
                    current_channel = 1;
                }
                ESP_LOGW(TAG, "Changing Wi-Fi channel to %d to force disconnect!", current_channel);
                esp_wifi_set_channel(current_channel, WIFI_SECOND_CHAN_NONE);
            }
        }
    }
}
