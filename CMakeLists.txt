idf_component_register(
    NAME espnow_manager
    SRCS
        "espnow_manager.cpp"
        "espnow_storage.cpp"
    
    INCLUDE_DIRS
        "include"
    
    REQUIRES
        esp_wifi
        esp_timer
        nvs_flash
        driver
    
    PRIV_REQUIRES
        esp_common
)

# Conditional compilation for unit testing
# if(CONFIG_UNIT_TESTING)
#     target_compile_definitions(${COMPONENT_LIB} INTERFACE "-DCONFIG_UNIT_TESTING=1")
# endif()