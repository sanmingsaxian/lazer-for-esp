idf_component_register(
    SRCS "audio_engine.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common library lyrics mp3dec audio_out netease netease_audio storage board freertos
)
