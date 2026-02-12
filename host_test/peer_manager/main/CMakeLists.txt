idf_component_register(
    SRCS
        "test_main.cpp"
        "test_peer_manager.cpp"
    INCLUDE_DIRS
        "."
        "../../../mocks"
    REQUIRES
        unity
        espnow_manager
        esp_wifi
        WHOLE_ARCHIVE
)


# Using coverage commands from NVS host test example: https://github.com/espressif/esp-idf/tree/v5.2.1/components/nvs_flash/host_test/nvs_page_test
target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
target_link_libraries(${COMPONENT_LIB} --coverage)
if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu++20)
endif()