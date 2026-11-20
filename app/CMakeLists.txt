idf_component_register(
    SRCS "app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common keyboard audio_engine library settings netease board freertos
)
