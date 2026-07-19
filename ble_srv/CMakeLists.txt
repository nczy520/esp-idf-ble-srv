set(ble_srv_srcs "src/ble_srv_core.c"
                 "src/ble_srv_gatt.c"
                 "src/ble_srv_ota_common.c"
                 "src/ble_srv_ota_bt.c"
                 "src/ble_srv_ota_url.c"
                 "src/ble_srv_device.c"
                 "src/ble_srv_led.c"
                 "src/ble_srv_wifi.c"
                 "src/ble_srv_ntp.c"
                 "src/ble_srv_temp_sensor.c"
                 "src/ble_srv_log.c")

idf_component_register(SRCS ${ble_srv_srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES bt app_update nvs_flash spi_flash esp_http_client esp_https_ota mbedtls littlefs vfs fatfs
                    PRIV_REQUIRES esp_driver_rmt esp_wifi esp_netif esp_http_server esp_driver_tsens esp_driver_sdmmc)
