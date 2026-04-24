# test_apps/test_build/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# The project root is two levels up from test_apps/test_build
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}"
    "$ENV{IDF_PATH}/components"
)

set(COMPONENTS main espnow_manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_build)
