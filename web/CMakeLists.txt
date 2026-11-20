idf_component_register(
    SRCS "web.cpp" "web_sd.cpp" "multipart.cpp"
    INCLUDE_DIRS "include"
    EMBED_FILES "www/index.html"
    REQUIRES common app audio_engine library settings netease wifi_mgr storage board
             esp_http_server mbedtls app_update freertos
)
