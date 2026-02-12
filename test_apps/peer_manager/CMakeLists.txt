cmake_minimum_required(VERSION 3.16)

# Include standard components
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/components")
list(APPEND EXTRA_COMPONENT_DIRS "../../..")

# Removed host mocks as they conflict with on-chip build (newlib/sys/stat.h issues)

set(COMPONENTS main espnow_manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(build_test)
