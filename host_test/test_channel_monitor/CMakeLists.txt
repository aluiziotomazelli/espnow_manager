# host_test/test_channel_monitor/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Project name for this test application.
project(test_channel_monitor)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_channel_monitor "*/espnow_manager/src/channel_monitor.cpp")

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_channel_monitor test_channel_monitor.elf)
