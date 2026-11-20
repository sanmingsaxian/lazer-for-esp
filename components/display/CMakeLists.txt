idf_component_register(
    SRCS "display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd lvgl board esp_timer freertos
)
