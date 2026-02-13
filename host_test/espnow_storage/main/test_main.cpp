#include "unity.h"
#include <stdio.h>
#include <stdlib.h>

extern "C" void app_main()
{
    UNITY_BEGIN();
    unity_run_all_tests();
    int failures = UNITY_END();
    exit(failures);
}
