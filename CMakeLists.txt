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
        "src/node_state_machine.cpp"
        "src/channel_monitor.cpp"
        "src/statistics_manager.cpp"

    INCLUDE_DIRS
        "include"
        "include/interfaces"

    REQUIRES
        esp_wifi
        esp_timer
        nvs_flash
        driver

    PRIV_REQUIRES
        esp_common
)

# ETL (Embedded Template Library) is header-only. We inline it here via FetchContent
# so that consumers of this component do not need any extra configuration.
# This block is guarded by NOT CMAKE_BUILD_EARLY_EXPANSION because FetchContent and
# target_include_directories are not available during the requirements-collection pass.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    # Cache the download in a shared directory inside the component so that all
    # projects reuse the same copy and do not trigger redundant downloads.
    set(SHARED_ETL_DIR "${CMAKE_CURRENT_LIST_DIR}/external/etl/etl-src")

    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        if(EXISTS "${SHARED_ETL_DIR}/include/etl/version.h")
            set(etl_SOURCE_DIR "${SHARED_ETL_DIR}" CACHE INTERNAL "")
            set(etl_POPULATED TRUE CACHE INTERNAL "")
            message(STATUS "ETL | Using shared source directory: ${SHARED_ETL_DIR}")
        else()
            message(STATUS "ETL | Shared source not found. Downloading...")
            FetchContent_Populate(
                etl
                URL                         https://github.com/ETLCPP/etl/archive/refs/tags/20.39.4.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP  TRUE
                SOURCE_DIR                  "${SHARED_ETL_DIR}"
            )
            # Remove heavy directories — ETL is header-only, only 'include' is needed.
            foreach(dir test examples doc images uml .git arduino scripts subprojects support)
                if(EXISTS "${SHARED_ETL_DIR}/${dir}")
                    file(REMOVE_RECURSE "${SHARED_ETL_DIR}/${dir}")
                endif()
            endforeach()
        endif()
    endif()

    # Expose ETL headers to this component and, transitively, to any target that
    # links against espnow_manager.
    target_include_directories(${COMPONENT_LIB} PUBLIC "${SHARED_ETL_DIR}/include")
endif()

idf_build_get_property(target IDF_TARGET)
if(${target} STREQUAL "linux")
    target_compile_options(${COMPONENT_LIB} PUBLIC --coverage)
    target_link_libraries(${COMPONENT_LIB} PUBLIC --coverage)
endif()