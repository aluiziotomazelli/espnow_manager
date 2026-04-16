# test_apps/test_multiple_devices/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# The project root is two levels up from test_apps/test_multiple_devices
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)


list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}" 
    "${PROJECT_ROOT}/external" 
    "$ENV{IDF_PATH}/components" 
    "$ENV{IDF_PATH}/tools/unit-test-app/components/test_utils" 
)



set(COMPONENTS main espnow_manager unity)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_multiple_devices)