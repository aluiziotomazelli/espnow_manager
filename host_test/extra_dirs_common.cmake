# host_test/setup_common.cmake
# Common setup for host-based test projects.
# This file is included by individual test projects to avoid duplication.

# The project root is one level up from this file (host_test/)
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Append extra component directories so IDF can find our local components.
list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}"
    "${PROJECT_ROOT}/host_test/gtest"
    "${PROJECT_ROOT}/host_test/common"
    "$ENV{IDF_PATH}/tools/mocks/esp_wifi"
    "$ENV{IDF_PATH}/tools/mocks/esp_netif"
    "$ENV{IDF_PATH}/tools/mocks/lwip"
    "$ENV{IDF_PATH}/tools/mocks/esp_timer"
    "$ENV{IDF_PATH}/tools/mocks/driver"
)

# Explicitly list the components to be included in the build.
set(COMPONENTS 
    "main"
    "espnow_manager"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)