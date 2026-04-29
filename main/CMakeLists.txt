idf_component_register(SRCS "main.c" "wifi_station.c" "spotify_auth.c" "spotify_api.c" "nvs_config.c"
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_wifi esp_http_client nvs_flash mbedtls
                       INCLUDE_DIRS "")
