idf_component_register(
    SRCS "board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_adc esp_timer esp_system
)
