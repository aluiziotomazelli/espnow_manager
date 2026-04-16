# host_test/test_tx_state_machine/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Common CMake configuration for all tests in the host_test directory.
include("../extra_dirs_common.cmake")

# Define the project name for this test application.
project(test_tx_state_machine)


# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_tx_state_machine "*/espnow_manager/src/tx_state_machine.cpp")

# Set the patterns to remove from the binary
# set(REMOVE_PATTERNS_test_tx_state_machine 
#
# )

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_tx_state_machine test_tx_state_machine.elf)
