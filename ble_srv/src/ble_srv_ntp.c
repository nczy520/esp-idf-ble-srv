#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_sntp.h"
#include "ble_srv_log.h"

static const char *TAG = "BLE_SRV_NTP";

#define NTP_MAX_RETRIES           10
#define NTP_RETRY_INTERVAL_MS     1000
#define NTP_DEINIT_WAIT_MS        2000

#ifdef CONFIG_BLE_SRV_NTP_ENABLED
static const char *ntp_servers[] = {
    CONFIG_BLE_SRV_NTP_SERVER_1,
    CONFIG_BLE_SRV_NTP_SERVER_2,
    CONFIG_BLE_SRV_NTP_SERVER_3,
    CONFIG_BLE_SRV_NTP_SERVER_4,
    CONFIG_BLE_SRV_NTP_SERVER_5,
};

static TaskHandle_t s_ntp_task = NULL;
static volatile bool s_ntp_stop = false;

static void sntp_time_sync_notification_cb(struct timeval *tv)
{
    BLE_SRV_LOGI(TAG, "NTP time sync completed");

    time_t now = time(NULL);
    struct tm timeinfo = {0};
    localtime_r(&now, &timeinfo);

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    BLE_SRV_LOGI(TAG, "Current local time: %s", strftime_buf);
}

static void ntp_sync_task(void *arg)
{
    TaskHandle_t caller = (TaskHandle_t)arg;
    BLE_SRV_LOGI(TAG, "NTP sync task started");

    if (esp_sntp_enabled()) {
        BLE_SRV_LOGI(TAG, "SNTP already running, stopping first");
        esp_sntp_stop();
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);

    for (int i = 0; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); i++) {
        esp_sntp_setservername(i, ntp_servers[i]);
    }

    esp_sntp_set_time_sync_notification_cb(sntp_time_sync_notification_cb);

    BLE_SRV_LOGI(TAG, "NTP servers configured:");
    for (int i = 0; i < sizeof(ntp_servers) / sizeof(ntp_servers[0]); i++) {
        BLE_SRV_LOGI(TAG, "  Server %d: %s", i + 1, ntp_servers[i]);
    }

    esp_sntp_init();

    setenv("TZ", CONFIG_BLE_SRV_NTP_TIMEZONE, 1);
    tzset();
    BLE_SRV_LOGI(TAG, "Timezone set to: %s", CONFIG_BLE_SRV_NTP_TIMEZONE);

    int retry = 0;
    const int max_retry = NTP_MAX_RETRIES;
    bool success = false;
    while (retry < max_retry && !s_ntp_stop) {
        time_t now = time(NULL);
        if (now > 1000000000) {
            BLE_SRV_LOGI(TAG, "NTP sync successful");
            success = true;
            break;
        }
        BLE_SRV_LOGI(TAG, "Waiting for NTP sync... (%d/%d)", retry + 1, max_retry);
        vTaskDelay(pdMS_TO_TICKS(NTP_RETRY_INTERVAL_MS));
        retry++;
    }

    if (s_ntp_stop) {
        BLE_SRV_LOGI(TAG, "NTP sync task stopped");
    } else if (success) {
        BLE_SRV_LOGI(TAG, "NTP sync completed successfully");
    } else {
        BLE_SRV_LOGE(TAG, "NTP sync timed out");
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }

    s_ntp_task = NULL;
    if (caller) {
        xTaskNotifyGive(caller);
    }
    vTaskDelete(NULL);
}
#endif

bool ble_srv_ntp_sync(void)
{
#ifdef CONFIG_BLE_SRV_NTP_ENABLED
    if (s_ntp_task) {
        BLE_SRV_LOGW(TAG, "NTP sync already in progress");
        return true;
    }

    s_ntp_stop = false;
    BaseType_t ret = xTaskCreate(ntp_sync_task, "ntp_sync", 4096,
                                  xTaskGetCurrentTaskHandle(), 2, &s_ntp_task);
    if (ret != pdPASS) {
        BLE_SRV_LOGE(TAG, "Failed to create NTP sync task");
        s_ntp_task = NULL;
        return false;
    }

    BLE_SRV_LOGI(TAG, "NTP sync started");
    return true;
#else
    BLE_SRV_LOGW(TAG, "NTP is not enabled in configuration");
    return false;
#endif
}

void ble_srv_ntp_deinit(void)
{
#ifdef CONFIG_BLE_SRV_NTP_ENABLED
    s_ntp_stop = true;

    TaskHandle_t task = s_ntp_task;
    if (task) {
        xTaskNotifyGive(task);
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NTP_DEINIT_WAIT_MS));
    }

    if (esp_sntp_enabled()) {
        esp_sntp_stop();
    }
    BLE_SRV_LOGI(TAG, "NTP deinitialized");
#endif
}
