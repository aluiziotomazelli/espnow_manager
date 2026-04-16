#include "unity.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_task_wdt.h"

static void wifi_init()
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

extern "C" void app_main(void)
{
    // Disable Task Watchdog to avoid triggers in Unity menu loop
    esp_task_wdt_deinit();

    // Init Wi-Fi since EspNowManager depends on it
    wifi_init();

    unity_run_menu();

    // UNITY_BEGIN();
    // unity_run_all_tests();
    // exit(UNITY_END());
}