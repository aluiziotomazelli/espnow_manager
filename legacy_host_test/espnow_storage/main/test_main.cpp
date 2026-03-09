#include "unity.h"
#include <stdio.h>
#include <stdlib.h>

extern "C" void app_main()
{
    UNITY_BEGIN();
    unity_run_all_tests();
    exit(UNITY_END());
}
