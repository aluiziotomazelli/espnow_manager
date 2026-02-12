#include "unity.h"
#include "esp_system.h"

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
    esp_restart();
}
