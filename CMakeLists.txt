idf_component_register(
    NAME espnow_manager
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

idf_build_get_property(target IDF_TARGET)
if(${target} STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_libraries(${COMPONENT_LIB} PUBLIC --coverage)
endif()   