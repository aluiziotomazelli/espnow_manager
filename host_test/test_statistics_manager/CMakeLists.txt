# host_test/test_storage_manager/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Project name for this test application.
project(test_statistics_manager)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_statistics_manager 
    "*/espnow_manager/src/statistics_manager.cpp"
    
)

# Set the patterns to remove from the binary
# set(REMOVE_PATTERNS_test_storage_manager 
#
# )

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_statistics_manager test_statistics_manager.elf)
