idf_component_register(
    SRCS "http_audio_source.cpp" "net_stream.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common mp3dec esp_http_client board freertos
)
