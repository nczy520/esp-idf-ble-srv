#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sntp.h"

static const char *TAG = "BLE_SRV_NTP";

static const char *ntp_servers[] = {
    CONFIG_BLE_SRV_NTP_SERVER_1,
    CONFIG_BLE_SRV_NTP_SERVER_2,
    CONFIG_BLE_SRV_NTP_SERVER_3,
    CONFIG_BLE_SRV_NTP_SERVER_4,
    CONFIG_BLE_SRV_NTP_SERVER_5,
};

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

bool ble_srv_ntp_sync(void)
{
    ESP_LOGI(TAG, "Starting NTP time synchronization...");

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
    while (retry < max_retry) {
        time_t now = time(NULL);
        if (now > 1000000000) {
            ESP_LOGI(TAG, "NTP sync successful!");
            return true;
        }
        ESP_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(1000));
        retry++;
    }

    ESP_LOGE(TAG, "NTP sync failed after %d retries", max_retry);
    return false;
}

void ble_srv_ntp_deinit(void)
{
    esp_sntp_stop();
    ESP_LOGI(TAG, "NTP deinitialized");
}