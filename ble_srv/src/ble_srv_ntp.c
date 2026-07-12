#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sntp.h"

static const char *TAG = "BLE_SRV_NTP";

#ifdef CONFIG_BLE_SRV_NTP_ENABLED
static const char *ntp_servers[] = {
    CONFIG_BLE_SRV_NTP_SERVER_1,
    CONFIG_BLE_SRV_NTP_SERVER_2,
    CONFIG_BLE_SRV_NTP_SERVER_3,
    CONFIG_BLE_SRV_NTP_SERVER_4,
    CONFIG_BLE_SRV_NTP_SERVER_5,
};

static volatile TaskHandle_t s_ntp_task = NULL;
static volatile bool s_ntp_stop = false;

static void sntp_time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time sync completed");

    time_t now = time(NULL);
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Current local time: %s", strftime_buf);
}

static void ntp_sync_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Starting NTP time synchronization...");

    if (esp_sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP already running, stopping first");
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    for (int i = 0; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); i++) {
        esp_sntp_setservername(i, ntp_servers[i]);
    }

    esp_sntp_set_time_sync_notification_cb(sntp_time_sync_notification_cb);

    ESP_LOGI(TAG, "NTP servers configured:");
    for (int i = 0; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); i++) {
        ESP_LOGI(TAG, "  Server %d: %s", i + 1, ntp_servers[i]);
    }

    esp_sntp_init();

    setenv("TZ", CONFIG_BLE_SRV_NTP_TIMEZONE, 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to: %s", CONFIG_BLE_SRV_NTP_TIMEZONE);

    int retry = 0;
    const int max_retry = 10;
    bool success = false;
    while (retry < max_retry && !s_ntp_stop) {
        time_t now = time(NULL);
        if (now > 1000000000) {
            ESP_LOGI(TAG, "NTP sync successful!");
            success = true;
            break;
        }
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    if (s_ntp_stop) {
        ESP_LOGI(TAG, "NTP sync stopped by deinit");
    } else if (success) {
        ESP_LOGI(TAG, "NTP sync completed successfully");
    } else {
        ESP_LOGE(TAG, "NTP sync timed out");
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    s_ntp_task = NULL;
    vTaskDelete(NULL);
}
#endif

bool ble_srv_ntp_sync(void)
{
#ifdef CONFIG_BLE_SRV_NTP_ENABLED
    if (s_ntp_task) {
        ESP_LOGW(TAG, "NTP sync already in progress");
        return true;
    }

    s_ntp_stop = false;
    BaseType_t ret = xTaskCreate(ntp_sync_task, "ntp_sync", 4096, NULL, 2, (TaskHandle_t *)&s_ntp_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create NTP sync task");
        s_ntp_task = NULL;
        return false;
    }

    ESP_LOGI(TAG, "NTP sync task started");
    return true;
#else
    ESP_LOGW(TAG, "NTP is not enabled in configuration");
    return false;
#endif
}

void ble_srv_ntp_deinit(void)
{
#ifdef CONFIG_BLE_SRV_NTP_ENABLED
    s_ntp_stop = true;

    uint32_t waited = 0;
    while (s_ntp_task && waited < 2000) {
        vTaskDelay(pdMS_TO_TICKS(20));
        waited += 20;
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    ESP_LOGI(TAG, "NTP deinitialized");
#endif
}
