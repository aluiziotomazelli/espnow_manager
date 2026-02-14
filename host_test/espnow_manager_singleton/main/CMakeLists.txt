idf_component_register(SRCS "test_main.cpp" "test_espnow_manager_singleton.cpp"
                    INCLUDE_DIRS "." "../../../mocks"
                    REQUIRES unity espnow_manager esp_wifi esp_timer nvs_flash WHOLE_ARCHIVE
                    PRIV_REQUIRES spi_flash)

target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
target_link_libraries(${COMPONENT_LIB} --coverage)
