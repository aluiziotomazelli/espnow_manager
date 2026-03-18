# host_test/test_storage_manager/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Project name for this test application.
project(test_storage_manager)

# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_storage_manager 
    "*/espnow_manager/src/storage_manager.cpp"
    "*/espnow_manager/src/persistence_backend.cpp"
    
)

# Set the patterns to remove from the binary
# set(REMOVE_PATTERNS_test_storage_manager 
#
# )

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_storage_manager test_storage_manager.elf)
