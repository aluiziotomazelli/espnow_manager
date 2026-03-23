cmake_minimum_required(VERSION 3.16)
project(espnow_manager_host_tests NONE)

# Enable CTest for this project
include(CTest)

# Define the tests for CTest.
# These commands assume that each project has been built using 'idf.py build'
# and that CTest is being run from a 'build' subdirectory within 'host_test'.
add_test(NAME test_espnow_driver COMMAND ../test_espnow_driver/build/test_espnow_driver.elf)
add_test(NAME test_espnow_manager COMMAND ../test_espnow_manager/build/test_espnow_manager.elf)
add_test(NAME test_discovery_manager COMMAND ../test_discovery_manager/build/test_discovery_manager.elf)
add_test(NAME test_heartbeat_manager COMMAND ../test_heartbeat_manager/build/test_heartbeat_manager.elf)
add_test(NAME test_message_codec COMMAND ../test_message_codec/build/test_message_codec.elf)
add_test(NAME test_message_router COMMAND ../test_message_router/build/test_message_router.elf)
add_test(NAME test_pairing_manager COMMAND ../test_pairing_manager/build/test_pairing_manager.elf)
add_test(NAME test_peer_manager COMMAND ../test_peer_manager/build/test_peer_manager.elf)
add_test(NAME test_storage_manager COMMAND ../test_storage_manager/build/test_storage_manager.elf)
add_test(NAME test_tx_manager COMMAND ../test_tx_manager/build/test_tx_manager.elf)
add_test(NAME test_tx_state_machine COMMAND ../test_tx_state_machine/build/test_tx_state_machine.elf)
add_test(NAME test_node_state_machine COMMAND ../test_node_state_machine/build/test_node_state_machine.elf)

# Unified Coverage Configuration
find_program(LCOV_PATH lcov REQUIRED)
find_program(GENHTML_PATH genhtml REQUIRED)

# The unified coverage report will be placed in host_test/coverage
set(UNIFIED_COVERAGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/coverage")

# This target aggregates coverage data (.gcda files) from all test build directories
add_custom_target(unified_coverage
    COMMAND ${LCOV_PATH} --capture --directory .. --output-file unified_coverage.info --rc branch_coverage=1 --rc geninfo_unexecuted_blocks=1 --ignore-errors mismatch,inconsistent
    COMMAND ${LCOV_PATH} --extract unified_coverage.info "*/espnow_manager/src/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty
    COMMAND ${LCOV_PATH} --remove unified_coverage_filtered.info "*/espnow_manager/include/interfaces/*" --output-file unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty
    COMMAND ${GENHTML_PATH} unified_coverage_filtered.info --output-directory ${UNIFIED_COVERAGE_DIR} --title "ESP-NOW Manager Unified Coverage" --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused
    COMMAND ${LCOV_PATH} --list unified_coverage_filtered.info --rc branch_coverage=1 --ignore-errors mismatch,inconsistent,unused,empty
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generating unified coverage report from all host tests"
)

# Added cleanup target to manually clear previous results if needed
add_custom_target(coverage_clean
    COMMAND find . -name "*.gcda" -delete
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${UNIFIED_COVERAGE_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Cleaning up coverage data"
)

# Helper target to build all tests at once (requires idf.py to be in your PATH)
add_custom_target(build_all_tests
    COMMAND idf.py -C ../test_espnow_driver build
    COMMAND idf.py -C ../test_espnow_manager build
    COMMAND idf.py -C ../test_discovery_manager build
    COMMAND idf.py -C ../test_heartbeat_manager build
    COMMAND idf.py -C ../test_message_codec build
    COMMAND idf.py -C ../test_message_router build
    COMMAND idf.py -C ../test_pairing_manager build
    COMMAND idf.py -C ../test_peer_manager build
    COMMAND idf.py -C ../test_storage_manager build
    COMMAND idf.py -C ../test_tx_manager build
    COMMAND idf.py -C ../test_tx_state_machine build
    COMMAND idf.py -C ../test_node_state_machine build
    COMMENT "Building all test projects using idf.py"
)

# Unified target to build, clean, run all tests and generate coverage
add_custom_target(run_all_tests

    COMMAND ${CMAKE_COMMAND} --build . --target build_all_tests
    COMMAND ${CMAKE_COMMAND} --build . --target coverage_clean
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMAND ${CMAKE_COMMAND} --build . --target unified_coverage
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Full test cycle: build, clean, run, and coverage"
)


