#include "esp_system.h"
#include "unity.h"

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    esp_restart();
}
