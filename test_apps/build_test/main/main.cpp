#include "espnow.hpp"
#include "esp_log.h"

extern "C" void app_main(void)
{
    ESP_LOGI("main", "Testing EspNow component compilation");
    // Just a basic usage to ensure linking and headers work
    EspNow &espnow = EspNow::instance();
    (void)espnow;
}
