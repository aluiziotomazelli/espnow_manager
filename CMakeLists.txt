idf_component_register(
    SRCS
        "espnow_manager.cpp"
        "espnow_storage.cpp"
        "peer_manager.cpp"
        "message_codec.cpp"
        "tx_state_machine.cpp"
        "channel_scanner.cpp"
        "wifi_hal.cpp"
        "tx_manager.cpp"
        "heartbeat_manager.cpp"
        "pairing_manager.cpp"
        "message_router.cpp"
    
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

# list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/components")

# Conditional compilation for unit testing
# if(CONFIG_UNIT_TESTING)
#     target_compile_definitions(${COMPONENT_LIB} INTERFACE "-DCONFIG_UNIT_TESTING=1")
# endif()