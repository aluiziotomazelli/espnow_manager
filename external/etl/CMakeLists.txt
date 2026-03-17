# ./external/etl/CMakeLists.txt
# CMake configuration for the ETL (Embedded Template Library) header-only library.

# Register this directory as an ESP-IDF component named 'etl'
idf_component_register()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    # Set policy CMP0169 to OLD to allow FetchContent_Populate to be called 
    # directly without deprecation warnings in CMake 3.30+.
    # This is necessary because we must bypass ETL's internal CMakeLists.txt
    # (which contains errors) while remaining future-proof for IDF updates.
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()

    # Use Git repository to ensure a .git directory is present, as requested.
    FetchContent_Declare(
        etl
        GIT_REPOSITORY https://github.com/ETLCPP/etl.git
        GIT_TAG        20.39.4
        GIT_SHALLOW    FALSE
    )

    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        FetchContent_Populate(etl)
        
        # ETL is header-only. We manually add its include directory.
        # This approach avoids ETL's internal CMake logic errors while 
        # satisfying the requirement for a Git-based source.
        target_include_directories(${COMPONENT_LIB} INTERFACE "${etl_SOURCE_DIR}/include")
    endif()
endif()
