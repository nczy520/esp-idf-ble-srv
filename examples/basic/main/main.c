#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "ble_srv.h"
#include "ble_srv_log.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    BLE_SRV_LOGI(TAG, "Initializing BLE Service Example");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!ble_srv_log_init()) {
        BLE_SRV_LOGE(TAG, "Log system initialization failed");
        return;
    }
    BLE_SRV_LOGI(TAG, "Log system initialized");

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    if (!ble_srv_wifi_provisioner_init()) {
        BLE_SRV_LOGE(TAG, "WiFi provisioner initialization failed");
        return;
    }
#endif

    if (!ble_srv_init()) {
        BLE_SRV_LOGE(TAG, "BLE Service initialization failed");
        return;
    }

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    ble_srv_wifi_auto_connect();
#endif

    BLE_SRV_LOGI(TAG, "BLE Service Example started successfully");

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    BLE_SRV_LOGI(TAG, "WiFi provisioner: started");
#endif

    while (1) {
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
        static bool last_wifi_connected = false;
        bool wifi_connected = ble_srv_wifi_is_connected();
        if (wifi_connected && !last_wifi_connected) {
            BLE_SRV_LOGI(TAG, "WiFi connected!");
            last_wifi_connected = true;
        } else if (!wifi_connected && last_wifi_connected) {
            BLE_SRV_LOGI(TAG, "WiFi disconnected!");
            last_wifi_connected = false;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
