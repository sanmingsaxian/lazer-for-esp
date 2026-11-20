file(GLOB_RECURSE LVGL_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.c)

idf_component_register(
    SRCS ${LVGL_SOURCES}
    INCLUDE_DIRS ${CMAKE_CURRENT_LIST_DIR}
    REQUIRES driver esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-function -Wno-format)
