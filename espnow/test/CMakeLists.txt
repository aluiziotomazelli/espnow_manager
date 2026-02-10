# components/espnow/test/CMakeLists.txt

idf_component_register(SRCS
    "test_espnow_storage.cpp"
    # "test_memory_helper.cpp"
    # "test_espnow_init.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        espnow
        unity
        nvs_flash
        esp_wifi
    PRIV_REQUIRES 
        freertos
        esp_timer
        test_utils
)

# Define UNIT_TESTING macro for test compilation
idf_build_set_property(COMPILE_DEFINITIONS "-DUNIT_TESTING=1" APPEND)
# add_compile_definitions(UNIT_TESTING=1)
# target_compile_definitions(${COMPONENT_LIB} PRIVATE "UNIT_TESTING=1")