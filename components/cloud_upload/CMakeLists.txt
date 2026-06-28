idf_component_register(
    SRCS "src/cloud_upload.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_event esp_http_client esp_netif json nvs_flash
    PRIV_REQUIRES esp_wifi
)
