#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "ble_srv.h"

static const char *TAG = "APP_MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing BLE Service Example");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    if (!ble_srv_wifi_provisioner_init()) {
        ESP_LOGE(TAG, "WiFi provisioner initialization failed");
        return;
    }
#endif

    if (!ble_srv_init()) {
        ESP_LOGE(TAG, "BLE Service initialization failed");
        return;
    }

    ESP_LOGI(TAG, "BLE Service Example started successfully");

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    ESP_LOGI(TAG, "WiFi provisioner: started");
#endif

    while (1) {
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
        static bool last_wifi_connected = false;
        bool wifi_connected = ble_srv_wifi_is_connected();
        if (wifi_connected && !last_wifi_connected) {
            ESP_LOGI(TAG, "WiFi connected!");
            last_wifi_connected = true;
        } else if (!wifi_connected && last_wifi_connected) {
            ESP_LOGI(TAG, "WiFi disconnected!");
            last_wifi_connected = false;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
