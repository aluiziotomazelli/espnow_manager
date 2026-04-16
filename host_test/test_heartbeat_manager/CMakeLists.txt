# host_test/test_heartbeat_manager/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Project name for this test application.
project(test_heartbeat_manager)


# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_heartbeat_manager "*/espnow_manager/src/heartbeat_manager.cpp")

# Set the patterns to remove from the binary
# set(REMOVE_PATTERNS_test_heartbeat_manager 
#
# )

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_heartbeat_manager test_heartbeat_manager.elf)
