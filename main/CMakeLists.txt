idf_component_register(SRCS "main.c" "wifi_station.c" "spotify_auth.c" "spotify_api.c" "nvs_config.c" "display.c" "tjpgd.c"
                       PRIV_REQUIRES spi_flash esp_driver_gpio esp_driver_spi esp_wifi esp_http_client nvs_flash mbedtls esp_lcd
                       INCLUDE_DIRS "")
