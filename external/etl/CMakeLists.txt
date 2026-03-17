# ./external/etl/CMakeLists.txt
# CMake configuration for the ETL (Embedded Template Library) header-only library.

# Register this directory as an ESP-IDF component named 'etl'
idf_component_register()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    # Use the low-level FetchContent_Populate command with the direct signature.
    # This signature (passing all download details) is NOT deprecated in CMake 3.30.
    # It allows us to download the content without calling add_subdirectory(),
    # which avoids errors in ETL's internal CMakeLists.txt (specifically
    # conflicting git_describe function with ESP-IDF).

    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        FetchContent_Populate(
            etl
            GIT_REPOSITORY https://github.com/ETLCPP/etl.git
            GIT_TAG        20.39.4
            GIT_SHALLOW    FALSE
        )
        
        # ETL is header-only. We manually add its include directory.
        target_include_directories(${COMPONENT_LIB} INTERFACE "${etl_SOURCE_DIR}/include")
    endif()
endif()
