#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "driver/gpio.h"
#include "espnow_manager.hpp"
#include "test_config.hpp"

using namespace espnow;

static const char* TAG = "FIELD_TEST_NODE";

static constexpr gpio_num_t BOOT_BUTTON_PIN = GPIO_NUM_0;

// I2C Configuration
static constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
static constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_4;
static constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_5;
static constexpr uint8_t SSD1306_ADDR = 0x3C;

static constexpr uint32_t LCD_PIXEL_CLOCK_HZ = 100000;
static constexpr int LCD_H_RES = 128;
static constexpr int LCD_V_RES = 64;

// Font 8x8 (Vertical mapping)
static const uint8_t font8x8[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Space
    0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x00, 0x00, // !
    0x00, 0x07, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00, // "
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x00, 0x00, 0x00, // #
    0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x00, 0x00, 0x00, // $
    0x23, 0x13, 0x08, 0x64, 0x62, 0x00, 0x00, 0x00, // %
    0x36, 0x49, 0x55, 0x22, 0x50, 0x00, 0x00, 0x00, // &
    0x00, 0x05, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, // '
    0x1C, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, // (
    0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, // )
    0x14, 0x08, 0x3E, 0x08, 0x14, 0x00, 0x00, 0x00, // *
    0x08, 0x08, 0x3E, 0x08, 0x08, 0x00, 0x00, 0x00, // +
    0x00, 0x50, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, // ,
    0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x00, // -
    0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, // .
    0x20, 0x10, 0x08, 0x04, 0x02, 0x00, 0x00, 0x00, // /
    0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x00, 0x00, // 0
    0x00, 0x42, 0x7F, 0x40, 0x00, 0x00, 0x00, 0x00, // 1
    0x42, 0x61, 0x51, 0x49, 0x46, 0x00, 0x00, 0x00, // 2
    0x21, 0x41, 0x45, 0x4B, 0x31, 0x00, 0x00, 0x00, // 3
    0x18, 0x14, 0x12, 0x7F, 0x10, 0x00, 0x00, 0x00, // 4
    0x27, 0x45, 0x45, 0x45, 0x39, 0x00, 0x00, 0x00, // 5
    0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00, 0x00, 0x00, // 6
    0x01, 0x71, 0x09, 0x05, 0x03, 0x00, 0x00, 0x00, // 7
    0x36, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00, // 8
    0x06, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00, 0x00, // 9
    0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, // :
    0x00, 0x56, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, // ;
    0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00, 0x00, // <
    0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00, 0x00, // =
    0x00, 0x41, 0x22, 0x14, 0x08, 0x00, 0x00, 0x00, // >
    0x02, 0x01, 0x51, 0x09, 0x06, 0x00, 0x00, 0x00, // ?
    0x32, 0x49, 0x79, 0x41, 0x3E, 0x00, 0x00, 0x00, // @
    0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00, 0x00, 0x00, // A
    0x7F, 0x49, 0x49, 0x49, 0x36, 0x00, 0x00, 0x00, // B
    0x3E, 0x41, 0x41, 0x41, 0x22, 0x00, 0x00, 0x00, // C
    0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00, 0x00, 0x00, // D
    0x7F, 0x49, 0x49, 0x49, 0x41, 0x00, 0x00, 0x00, // E
    0x7F, 0x09, 0x09, 0x09, 0x01, 0x00, 0x00, 0x00, // F
    0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00, 0x00, 0x00, // G
    0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x00, 0x00, // H
    0x00, 0x41, 0x7F, 0x41, 0x00, 0x00, 0x00, 0x00, // I
    0x20, 0x40, 0x41, 0x3F, 0x01, 0x00, 0x00, 0x00, // J
    0x7F, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00, // K
    0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, // L
    0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00, 0x00, 0x00, // M
    0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00, 0x00, 0x00, // N
    0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00, 0x00, 0x00, // O
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x00, 0x00, 0x00, // P
    0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00, 0x00, 0x00, // Q
    0x7F, 0x09, 0x19, 0x29, 0x46, 0x00, 0x00, 0x00, // R
    0x46, 0x49, 0x49, 0x49, 0x31, 0x00, 0x00, 0x00, // S
    0x01, 0x01, 0x7F, 0x01, 0x01, 0x00, 0x00, 0x00, // T
    0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00, 0x00, 0x00, // U
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00, 0x00, 0x00, // V
    0x3F, 0x40, 0x38, 0x40, 0x3F, 0x00, 0x00, 0x00, // W
    0x63, 0x14, 0x08, 0x14, 0x63, 0x00, 0x00, 0x00, // X
    0x07, 0x08, 0x70, 0x08, 0x07, 0x00, 0x00, 0x00, // Y
    0x61, 0x51, 0x49, 0x45, 0x43, 0x00, 0x00, 0x00, // Z
};

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

static esp_lcd_panel_handle_t lcd_init()
{
    static i2c_master_bus_handle_t i2c_bus = NULL;
    static esp_lcd_panel_io_handle_t io_handle = NULL;
    static esp_lcd_panel_handle_t panel_handle = NULL;

    // Clean up if re-initializing
    if (panel_handle)
        esp_lcd_panel_del(panel_handle);
    if (io_handle)
        esp_lcd_panel_io_del(io_handle);
    if (i2c_bus)
        i2c_del_master_bus(i2c_bus);

    i2c_master_bus_config_t bus_config = {};
    bus_config.i2c_port = I2C_PORT;
    bus_config.sda_io_num = I2C_SDA_PIN;
    bus_config.scl_io_num = I2C_SCL_PIN;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &i2c_bus);
    if (err != ESP_OK)
        return NULL;

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = SSD1306_ADDR;
    io_config.scl_speed_hz = LCD_PIXEL_CLOCK_HZ;
    io_config.control_phase_bytes = 1;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.dc_bit_offset = 6;

    err = esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle);
    if (err != ESP_OK)
        return NULL;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.bits_per_pixel = 1;
    panel_config.reset_gpio_num = -1;

    esp_lcd_panel_ssd1306_config_t ssd1306_config = {};
    ssd1306_config.height = LCD_V_RES;
    panel_config.vendor_config = &ssd1306_config;

    err = esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle);
    if (err != ESP_OK)
        return NULL;

    esp_lcd_panel_reset(panel_handle);
    vTaskDelay(pdMS_TO_TICKS(10));
    err = esp_lcd_panel_init(panel_handle);
    if (err != ESP_OK)
        return NULL;

    esp_lcd_panel_disp_on_off(panel_handle, true);
    return panel_handle;
}

static void draw_text(uint8_t* fb, int x, int y, const char* str)
{
    int page = y / 8;
    if (page >= 8)
        return;
    while (*str && x < 128) {
        uint8_t c = (uint8_t)*str;
        if (c >= 32 && c <= 90) {
            uint8_t idx = c - 32;
            for (int i = 0; i < 8; i++) {
                if (x + i < 128) {
                    fb[page * 128 + (x + i)] = font8x8[idx * 8 + i];
                }
            }
        }
        x += 8;
        str++;
    }
}

static const char* state_to_str(NodeState state)
{
    switch (state) {
    case NodeState::IDLE:
        return "IDLE";
    case NodeState::PAIRING:
        return "PAIRING";
    case NodeState::OPERATIONAL:
        return "OPERATIONAL";
    case NodeState::PAIRING_SCAN:
        return "P-SCAN";
    case NodeState::RECOVERY_SCAN:
        return "R-SCAN";
    default:
        return "UNKNOWN";
    }
}

extern "C" void app_main(void)
{
    // Always clear NVS for field test Node to ensure clean state
    ESP_LOGW(TAG, "Field test Node: Performing mandatory NVS erase for clean start...");
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
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_lcd_panel_handle_t panel = lcd_init();
    if (!panel) {
        ESP_LOGE(TAG, "Initial LCD init failed");
    }

    EspNowConfig config;
    config.node_id = field_test::NODE_ID;
    config.node_type = field_test::NODE_TYPE;
    config.wifi_channel = 1;
    config.app_rx_queue = xQueueCreate(10, sizeof(AppMessage));

    EspNowManager& manager = EspNowManager::instance();
    ESP_ERROR_CHECK(manager.init(config));

    // Force pairing mode on boot for 30 seconds
    ESP_LOGI(TAG, "Entering pairing mode for 30s...");
    manager.start_pairing(30000);

    uint32_t counter = 0;
    static uint8_t frame_buffer[LCD_H_RES * LCD_V_RES / 8];

    while (true) {
        // Check BOOT button for manual NVS erase
        if (gpio_get_level(BOOT_BUTTON_PIN) == 0) {
            ESP_LOGW(TAG, "BOOT button pressed! Erasing NVS and restarting...");
            nvs_flash_erase();
            esp_restart();
        }

        uint8_t payload[4];
        memcpy(payload, &counter, sizeof(uint32_t));
        manager.send_data(ReservedIds::HUB, field_test::TEST_PAYLOAD_TYPE, payload, sizeof(payload), true);
        counter++;

        PeerStatistics stats;
        bool has_stats = manager.get_peer_stats(ReservedIds::HUB, stats);
        NodeState current_state = manager.get_node_state();

        memset(frame_buffer, 0, sizeof(frame_buffer));
        char buf[64];
        if (has_stats) {
            snprintf(buf, sizeof(buf), "RSSI:%d AVG:%d", stats.rssi_last, stats.rssi_avg);
            draw_text(frame_buffer, 0, 0, buf);
            snprintf(
                buf, sizeof(buf), "S:%lu L:%lu", (unsigned long)stats.packets_sent, (unsigned long)stats.packets_lost);
            draw_text(frame_buffer, 0, 16, buf);

            snprintf(
                buf, sizeof(buf), "RTT:%lu AVG:%lu", (unsigned long)stats.rtt_last_us, (unsigned long)stats.rtt_avg_us);
            draw_text(frame_buffer, 0, 32, buf);

            ESP_LOGI(
                TAG,
                "RSSI: %d AVG: %d S: %lu L: %lu RTT: %lu AVG: %lu",
                stats.rssi_last,
                stats.rssi_avg,
                (unsigned long)stats.packets_sent,
                (unsigned long)stats.packets_lost,
                (unsigned long)stats.rtt_last_us,
                (unsigned long)stats.rtt_avg_us);
        }
        else {
            draw_text(frame_buffer, 0, 0, "WAITING HUB...");
        }

        snprintf(buf, sizeof(buf), "ST: %s", state_to_str(current_state));
        draw_text(frame_buffer, 0, 48, buf);

        if (panel) {
            esp_err_t draw_err = esp_lcd_panel_draw_bitmap(panel, 0, 0, LCD_H_RES, LCD_V_RES, frame_buffer);
            if (draw_err != ESP_OK) {
                ESP_LOGW(TAG, "Draw failed (%s), retrying init...", esp_err_to_name(draw_err));
                panel = lcd_init();
            }
        }
        else {
            panel = lcd_init();
        }

        vTaskDelay(pdMS_TO_TICKS(field_test::SEND_INTERVAL_MS));
    }
}
