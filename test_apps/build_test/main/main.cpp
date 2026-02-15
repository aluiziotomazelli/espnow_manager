#include "esp_log.h"
#include "espnow_manager.hpp"
#include "espnow_manager_interface.hpp"

extern "C" void app_main(void)
{
    ESP_LOGI("main", "Testing EspNow component compilation");
    // Just a basic usage to ensure linking and headers work
    EspNowManager &espnow = EspNowManager::instance();
    (void)espnow;
}
