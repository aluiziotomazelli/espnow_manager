cmake_minimum_required(VERSION 3.16)

# Append extra component directories so IDF can find our local components.
list(APPEND EXTRA_COMPONENT_DIRS
    "../.."
    "../gtest"
    "../common"
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    # "$ENV{IDF_PATH}/tools/mocks/esp_event"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
    "$ENV{IDF_PATH}/tools/mocks/esp_timer"
    "$ENV{IDF_PATH}/tools/mocks/driver"

)

# Explicitly list the components to be included in the build.
set(COMPONENTS main espnow_manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(test_peer_manager)


# Set the patterns to extract from the binary
set(EXTRACT_PATTERNS_test_peer_manager "*/espnow_manager/src/peer_manager.cpp")

# Set the patterns to remove from the binary
# set(REMOVE_PATTERNS_test_peer_manager 
#
# )

# Include common coverage logic
include("../coverage_common.cmake")
setup_gtest_coverage(test_peer_manager test_peer_manager.elf)
