cmake_minimum_required(VERSION 3.16)

# set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/../../../espnow_manager)   
list(APPEND EXTRA_COMPONENT_DIRS "../../..")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/components")

set(COMPONENTS main espnow_manager)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(build_test)
