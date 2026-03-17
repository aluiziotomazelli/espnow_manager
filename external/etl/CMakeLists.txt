# ./external/etl/CMakeLists.txt
# CMake configuration for the ETL (Embedded Template Library) header-only library.

# Register this directory as an ESP-IDF component named 'etl'
idf_component_register()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    # Use the low-level FetchContent_Populate command with the direct signature.
    # This avoids triggering the FetchContent_Populate(<name>) deprecation in CMake 3.30+.
    #
    # To address the size concerns (23MB+ per project):
    # 1. GIT_SHALLOW is set to TRUE to avoid downloading full history.
    # 2. We use a URL to download a ZIP/Tarball instead of a Git repository.
    #    This prevents the creation of a large .git directory (~16MB) and avoids
    #    issues with CMake's Git update step if we manually clean the source.

    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        FetchContent_Populate(
            etl
            URL                         https://github.com/ETLCPP/etl/archive/refs/tags/20.39.4.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP  TRUE
        )

        # Optimization: Remove heavy directories after extraction to save space.
        # This is safe because ETL is header-only and we only need the 'include' folder.
        # Since we use a URL download, there is no .git folder to worry about.
        set(DIRS_TO_REMOVE "test" "examples" "doc" "images" "uml" "arduino" "scripts" "subprojects" "support")
        foreach(dir ${DIRS_TO_REMOVE})
            if(EXISTS "${etl_SOURCE_DIR}/${dir}")
                file(REMOVE_RECURSE "${etl_SOURCE_DIR}/${dir}")
            endif()
        endforeach()
        
        # ETL is header-only. We manually add its include directory.
        target_include_directories(${COMPONENT_LIB} INTERFACE "${etl_SOURCE_DIR}/include")
    endif()
endif()
