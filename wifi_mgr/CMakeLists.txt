idf_component_register(
    SRCS "wifi_mgr.cpp"
    INCLUDE_DIRS "include"
    REQUIRES settings common board esp_wifi esp_netif esp_event freertos
)
