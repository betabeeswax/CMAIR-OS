idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bt driver esp_driver_ledc esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi esp_driver_sdmmc esp_lcd fatfs sdmmc esp_wifi esp_event esp_netif esp_http_client esp_timer nvs_flash json mbedtls
)

# The Bluedroid GAP implementation is inside libbt.a. Force the three GAP
# entry points into the link so GNU ld does not discard their archive objects
# before main.cpp's references are encountered.
target_link_options(${COMPONENT_LIB} INTERFACE
    "-Wl,-u,esp_ble_gap_set_scan_params"
    "-Wl,-u,esp_ble_gap_start_scanning"
    "-Wl,-u,esp_ble_gap_stop_scanning"
)
