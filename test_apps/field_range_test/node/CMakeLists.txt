cmake_minimum_required(VERSION 3.16)

# The project root is three levels up from test_apps/field_range_test/node
get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

list(APPEND EXTRA_COMPONENT_DIRS
    "${PROJECT_ROOT}"
)

set(COMPONENTS main espnow_manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(field_range_test_node)
