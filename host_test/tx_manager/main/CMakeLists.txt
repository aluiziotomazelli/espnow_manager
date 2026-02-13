idf_component_register(SRCS "test_main.cpp" "test_tx_manager.cpp"
                    INCLUDE_DIRS "." "../../../mocks"
                    REQUIRES unity app esp_wifi WHOLE_ARCHIVE)

target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
target_link_libraries(${COMPONENT_LIB} --coverage)
