idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi esp_driver_sdmmc esp_lcd fatfs sdmmc esp_wifi esp_event esp_netif esp_http_client nvs_flash json
)