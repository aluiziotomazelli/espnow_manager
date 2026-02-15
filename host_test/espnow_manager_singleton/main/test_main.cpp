#include "unity.h"
#include <stdlib.h>

extern "C" void app_main(void)
{
    UNITY_BEGIN();
    unity_run_all_tests();
    exit(UNITY_END());
}
