set(SOURCES "main.cpp")
set(INCLUDE_DIRS "." "../../common")

idf_component_register(SRCS ${SOURCES}
                       INCLUDE_DIRS ${INCLUDE_DIRS}
                       REQUIRES espnow_manager esp_lcd driver)
