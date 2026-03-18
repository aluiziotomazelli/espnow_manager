# ./external/etl/CMakeLists.txt
# CMake configuration for the ETL (Embedded Template Library) header-only library.

# Register this directory as an ESP-IDF component named 'etl'
idf_component_register()

if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include(FetchContent)

    # We use the direct signature of FetchContent_Populate to avoid deprecation warnings.
    # To address the size and redundant download concerns:
    # 1. SOURCE_DIR is set to a shared location within the component directory.
    #    This ensures all projects using this component share the same ETL download.
    # 2. We download via URL (Tarball) to save space (no .git folder).
    # 3. We manually remove heavy directories after population.

    # Shared source directory path
    set(SHARED_ETL_DIR "${CMAKE_CURRENT_LIST_DIR}/etl-src")

    FetchContent_GetProperties(etl)
    if(NOT etl_POPULATED)
        # Optimization: If the headers already exist in the shared directory, skip population.
        # This prevents multiple projects from conflicting when building in parallel
        # or redundant downloads.
        if(EXISTS "${SHARED_ETL_DIR}/include/etl/version.h")
            set(etl_SOURCE_DIR "${SHARED_ETL_DIR}" CACHE INTERNAL "")
            set(etl_POPULATED TRUE CACHE INTERNAL "")
            message(STATUS "ETL | Using shared source directory: ${SHARED_ETL_DIR}")
        else()
            message(STATUS "ETL | Shared source not found or incomplete. Downloading...")
            FetchContent_Populate(
                etl
                URL                         https://github.com/ETLCPP/etl/archive/refs/tags/20.39.4.tar.gz
                DOWNLOAD_EXTRACT_TIMESTAMP  TRUE
                SOURCE_DIR                  "${SHARED_ETL_DIR}"
            )

            # Optimization: Remove heavy directories after download to save space.
            # This is safe because ETL is header-only and we only need the 'include' folder.
            set(DIRS_TO_REMOVE "test" "examples" "doc" "images" "uml" ".git" "arduino" "scripts" "subprojects" "support")
            foreach(dir ${DIRS_TO_REMOVE})
                if(EXISTS "${etl_SOURCE_DIR}/${dir}")
                    file(REMOVE_RECURSE "${etl_SOURCE_DIR}/${dir}")
                endif()
            endforeach()
        endif()
    endif()

    # ETL is header-only. We manually add its include directory.
    # IMPORTANT: This must be outside the population block so it runs on every configuration!
    target_include_directories(${COMPONENT_LIB} INTERFACE "${SHARED_ETL_DIR}/include")
endif()
