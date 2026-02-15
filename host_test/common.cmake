get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# Use a symlink to force the component name to 'espnow_manager' regardless of the actual folder name
execute_process(COMMAND mkdir -p /tmp/espnow_comp)
execute_process(COMMAND ln -sf ${ROOT_DIR} /tmp/espnow_comp/espnow_manager)

set(EXTRA_COMPONENT_DIRS "/tmp/espnow_comp")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/components")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/esp_wifi")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/esp_netif")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/lwip")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/driver")
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/esp_timer")

set(COMPONENTS main espnow_manager)
