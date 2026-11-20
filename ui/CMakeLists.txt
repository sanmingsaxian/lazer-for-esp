idf_component_register(
    SRCS "ui.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lvgl fonts app audio_engine library lyrics netease settings web board common qrcode
)
