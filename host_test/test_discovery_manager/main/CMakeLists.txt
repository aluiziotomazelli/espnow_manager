idf_component_register(
    SRCS
        "main.cpp"
        "test_discovery_manager.cpp"
    INCLUDE_DIRS
        "."
        "../../common"
    REQUIRES
        gtest
        espnow_manager
    WHOLE_ARCHIVE
)

if(IDF_TARGET STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_options(${COMPONENT_LIB} PUBLIC --coverage)
endif()
