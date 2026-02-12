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

# Use linker wrapping to mock esp_now functions
target_link_libraries(${COMPONENT_LIB} INTERFACE 
    "-Wl,--wrap=esp_now_add_peer"
    "-Wl,--wrap=esp_now_del_peer"
    "-Wl,--wrap=esp_now_mod_peer"
)