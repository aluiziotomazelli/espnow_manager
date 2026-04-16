#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "espnow_manager.hpp"
#include "test_config.hpp"

static const char* TAG = "FIELD_TEST_HUB";

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
}

extern "C" void app_main(void)
{
    // Clear NVS to ensure a fresh start
    ESP_ERROR_CHECK(nvs_flash_erase());
    
    wifi_init();

    QueueHandle_t app_queue = xQueueCreate(20, sizeof(AppMessage));

    EspNowConfig config;
    config.node_id = ReservedIds::HUB;
    config.node_type = ReservedTypes::HUB;
    config.wifi_channel = 1;
    config.app_rx_queue = app_queue;

    EspNowManager& manager = EspNowManager::instance();
    esp_err_t err = manager.init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize EspNowManager: %s", esp_err_to_name(err));
        return;
    }

    manager.start_pairing(0xFFFFFFFF); 

    ESP_LOGI(TAG, "HUB re-initialized. Waiting for messages...");

    AppMessage msg;
    while (true) {
        // Block indefinitely until a message arrives
        if (xQueueReceive(app_queue, &msg, portMAX_DELAY) == pdTRUE) {
            
            if (msg.requires_ack) {
                manager.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
            }

            // Print info only on reception
            PeerStatistics stats;
            if (manager.get_peer_stats(msg.sender_id, stats)) {
                printf("Node %d | Seq: %u | RSSI: %d dBm | Avg: %d | RX: %lu | S: %lu | L: %lu\n",
                       msg.sender_id, msg.sequence_number, stats.rssi_last, stats.rssi_avg,
                       (unsigned long)stats.packets_rx, (unsigned long)stats.packets_sent,
                       (unsigned long)stats.packets_lost);
            }
        }
    }
}
