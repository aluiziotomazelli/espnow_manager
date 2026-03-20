# host_test/test_espnow_driver/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Define the project name for this test application.
project(test_espnow_driver)


# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_espnow_driver 
    "*/espnow_manager/src/espnow_driver.cpp"
)

# Set the patterns to remove from the binary
# set(REMOVE_PATTERNS_test_espnow_driver 
#
# )

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_espnow_driver test_espnow_driver.elf)
