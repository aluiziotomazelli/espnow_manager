# test_apps/build_test/CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# The project root is two levels up from test_apps/build_test
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../" ABSOLUTE)

list(APPEND EXTRA_COMPONENT_DIRS 
    "${PROJECT_ROOT}" 
    "${PROJECT_ROOT}/external"
    "$ENV{IDF_PATH}/components"
)

set(COMPONENTS main espnow_manager etl)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(build_test)
