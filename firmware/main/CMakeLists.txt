idf_component_register(
    SRCS "firmware.c"
    INCLUDE_DIRS "."
    REQUIRES esp_adc nvs_flash esp_wifi esp_http_client
)
