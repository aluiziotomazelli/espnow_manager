idf_component_register(
    SRCS 
        "main.c"
        "test_espnow_storage.cpp"
        # "test_memory_helper.cpp"
    INCLUDE_DIRS
        "."
        ".."
    REQUIRES 
        unity
        espnow_manager
        WHOLE_ARCHIVE
        

)
