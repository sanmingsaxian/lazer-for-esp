if(CONFIG_LAZER_LCD_TEST)
    set(MAIN_SRCS "lcd_test_main.cpp")
    set(MAIN_REQUIRES board display lvgl fonts freertos esp_system)
else()
    set(MAIN_SRCS "main.cpp")
    set(MAIN_REQUIRES board display keyboard app audio_engine library settings ui web
                      lvgl fonts nvs_flash freertos esp_system)
endif()

idf_component_register(
    SRCS ${MAIN_SRCS}
    INCLUDE_DIRS "."
    REQUIRES ${MAIN_REQUIRES}
)
