// test_memory_helper.hpp
#pragma once
#include "memory_checks.h"

class TestMemoryHelper
{
public:
    static void set_default_limits(void)
    {
        test_utils_set_leak_level(1200, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(0, ESP_LEAK_TYPE_WARNING, ESP_COMP_LEAK_GENERAL);
    }

    static void set_1kb_limits(void)
    {
        test_utils_set_leak_level(1024, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(512, ESP_LEAK_TYPE_WARNING, ESP_COMP_LEAK_GENERAL);
    }

    static void set_2kb_limits(void)
    {
        test_utils_set_leak_level(2048, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(1024, ESP_LEAK_TYPE_WARNING,
                                  ESP_COMP_LEAK_GENERAL);
    }
    static void set_4kb_limits(void)
    {
        test_utils_set_leak_level(4096, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(2048, ESP_LEAK_TYPE_WARNING,
                                  ESP_COMP_LEAK_GENERAL);
    }

    static void set_8kb_limits(void)
    {
        test_utils_set_leak_level(8192, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(4096, ESP_LEAK_TYPE_WARNING,
                                  ESP_COMP_LEAK_GENERAL);
    }

    static void set_16kb_limits(void)
    {
        test_utils_set_leak_level(16384, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(8192, ESP_LEAK_TYPE_WARNING,
                                  ESP_COMP_LEAK_GENERAL);
    }

    static void set_32kb_limits(void)
    {
        test_utils_set_leak_level(32768, ESP_LEAK_TYPE_CRITICAL,
                                  ESP_COMP_LEAK_GENERAL);
        test_utils_set_leak_level(16384, ESP_LEAK_TYPE_WARNING,
                                  ESP_COMP_LEAK_GENERAL);
    }
};
