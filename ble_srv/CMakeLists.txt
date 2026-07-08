set(ble_srv_srcs "src/ble_srv_core.c"
                 "src/ble_srv_gatt.c"
                 "src/ble_srv_ota.c"
                 "src/ble_srv_device.c")

if(CONFIG_BLE_SRV_WIFI_PROVISIONER)
    list(APPEND ble_srv_srcs "src/ble_srv_wifi.c")
endif()

if(CONFIG_BLE_SRV_NTP_SYNC)
    list(APPEND ble_srv_srcs "src/ble_srv_ntp.c")
endif()

if(CONFIG_BLE_SRV_LED)
    list(APPEND ble_srv_srcs "src/ble_srv_led.c")
endif()

idf_component_register(SRCS ${ble_srv_srcs}
                    INCLUDE_DIRS "include"
                    REQUIRES bt app_update nvs_flash spi_flash
                    PRIV_REQUIRES esp_driver_rmt esp_wifi esp_netif esp_http_server)
