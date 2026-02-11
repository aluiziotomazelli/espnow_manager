cmake_minimum_required(VERSION 3.16)
list(APPEND EXTRA_COMPONENT_DIRS "../..")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/components")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/esp_wifi")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/esp_netif")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/lwip")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/driver")

set(COMPONENTS main app)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(peer_manager_host_test)
