idf_component_register(SRCS "test_main.cpp" "test_message_router.cpp"
                    INCLUDE_DIRS "." "../../../mocks"
                    REQUIRES unity espnow_manager esp_wifi esp_timer WHOLE_ARCHIVE)

target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
target_link_libraries(${COMPONENT_LIB} --coverage)
