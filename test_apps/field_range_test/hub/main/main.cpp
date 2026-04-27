#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "led_strip.h"
#include "espnow_manager.hpp"
#include "test_config.hpp"

using namespace espnow;

static const char* TAG = "FIELD_TEST_HUB";

static constexpr gpio_num_t BOOT_BUTTON_PIN = GPIO_NUM_0;
static constexpr gpio_num_t LED_RGB_GPIO = GPIO_NUM_48;

static led_strip_handle_t led_strip;

static void init_led()
{
    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = LED_RGB_GPIO;
    strip_config.max_leds = 1;
    strip_config.led_model = LED_MODEL_WS2812;
    strip_config.color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB;
    strip_config.flags.invert_out = false;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10MHz
    rmt_config.flags.with_dma = false;

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    led_strip_clear(led_strip);
}

static void set_led_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (led_strip) {
        led_strip_set_pixel(led_strip, 0, r, g, b);
        led_strip_refresh(led_strip);
    }
}

static void blink_task(void* arg)
{
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        set_led_color(0, 255, 0); // Green
        vTaskDelay(pdMS_TO_TICKS(50));
        // Restore based on state
        if (EspNowManager::instance().get_node_state() == NodeState::PAIRING) {
            set_led_color(255, 255, 25); // Yellow-ish
        }
        else {
            set_led_color(0, 0, 0);
        }
    }
}

static TaskHandle_t blink_task_handle = nullptr;

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
    // Always clear NVS for field test Hub to ensure clean peer list
    ESP_LOGW(TAG, "Field test Hub: Performing mandatory NVS erase for clean start...");
    nvs_flash_erase();

    // Configure BOOT button
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << BOOT_BUTTON_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    wifi_init();
    init_led();
    xTaskCreate(blink_task, "blink_task", 2048, nullptr, 10, &blink_task_handle);

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

    // Force pairing mode on boot for 30 seconds
    ESP_LOGI(TAG, "Entering pairing mode for 30s...");
    manager.start_pairing(30000);

    ESP_LOGI(TAG, "HUB initialized. Waiting for messages...");

    AppMessage msg;
    NodeState last_state = manager.get_node_state();

    while (true) {
        // Check BOOT button for manual NVS erase
        if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
            ESP_LOGW(TAG, "BOOT button pressed! Erasing NVS and restarting...");
            nvs_flash_erase();
            esp_restart();
        }

        NodeState current_state = manager.get_node_state();
        if (current_state != last_state) {
            if (current_state == NodeState::PAIRING) {
                set_led_color(255, 255, 25); // Yellow-ish
            }
            else {
                set_led_color(0, 0, 0); // Off
            }
            last_state = current_state;
        }

        // Block with timeout to allow checking the button
        if (xQueueReceive(app_queue, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            if (msg.requires_ack) {
                esp_err_t ack_err = manager.confirm_reception(msg.sender_id, msg.sequence_number, AckStatus::OK);
                if (ack_err == ESP_ERR_NOT_FOUND) {
                    ESP_LOGW(TAG, "Received message from unregistered Node %d. Please re-pair.", msg.sender_id);
                }
            }

            if (blink_task_handle) {
                xTaskNotifyGive(blink_task_handle);
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
