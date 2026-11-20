idf_component_register(
    SRCS "netease.cpp" "netease_parse.cpp"
    INCLUDE_DIRS "include"
    REQUIRES common settings wifi_mgr board json nvs_flash esp_http_client freertos
)
