idf_component_register(
    NAME espnow_manager
    SRCS
        "src/espnow_manager.cpp"
        "src/storage_manager.cpp"
        "src/persistence_backend.cpp"
        "src/peer_manager.cpp"
        "src/message_codec.cpp"
        "src/tx_state_machine.cpp"
        "src/discovery_manager.cpp"
        "src/tx_manager.cpp"
        "src/heartbeat_manager.cpp"
        "src/pairing_manager.cpp"
        "src/message_router.cpp"
        "src/espnow_driver.cpp"
    
    INCLUDE_DIRS
        "include"
        "include/interfaces"
    
    REQUIRES
        esp_wifi
        esp_timer
        nvs_flash
        driver
        etl
    
    PRIV_REQUIRES
        esp_common
)

idf_build_get_property(target IDF_TARGET)
if(${target} STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_libraries(${COMPONENT_LIB} PUBLIC --coverage)
endif()   