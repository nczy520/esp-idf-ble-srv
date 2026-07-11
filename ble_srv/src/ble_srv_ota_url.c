#include "ble_srv_ota_url.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_gatt.h"
#include "ble_srv_wifi.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"

static const char *TAG = "OTA_URL";

#define OTA_URL_TASK_STACK    8192
#define OTA_URL_TASK_PRIO     5
#define FW_HEADER_BUF_SIZE    4096
#define HTTP_TIMEOUT_MS       2000
#define HTTP_BUFFER_SIZE      2048
#define LOOP_TICK_MS          50

static TaskHandle_t s_url_task = NULL;
static char s_ota_url[BLE_OTA_URL_MAX_URL_LEN + 1] = {0};

static bool parse_version(const char *ver_str, uint8_t *major, uint8_t *minor, uint8_t *patch)
{
    if (!ver_str || !major || !minor || !patch) return false;
    int m = 0, n = 0, p = 0;
    if (sscanf(ver_str, "%d.%d.%d", &m, &n, &p) == 3) {
        *major = (uint8_t)m; *minor = (uint8_t)n; *patch = (uint8_t)p; return true;
    }
    if (sscanf(ver_str, "%d.%d", &m, &n) == 2) {
        *major = (uint8_t)m; *minor = (uint8_t)n; *patch = 0; return true;
    }
    if (sscanf(ver_str, "%d", &m) == 1) {
        *major = (uint8_t)m; *minor = 0; *patch = 0; return true;
    }
    return false;
}

static bool version_newer(uint8_t cm, uint8_t cn, uint8_t cp,
                           uint8_t nm, uint8_t nn, uint8_t np)
{
    if (nm > cm) return true;
    if (nm < cm) return false;
    if (nn > cn) return true;
    if (nn < cn) return false;
    return np > cp;
}

static bool find_app_desc(const uint8_t *buf, size_t len, esp_app_desc_t *desc)
{
    if (!buf || !desc || len < sizeof(esp_app_desc_t)) return false;
    for (size_t i = 0; i <= len - sizeof(esp_app_desc_t); i += 4) {
        uint32_t magic;
        memcpy(&magic, buf + i, sizeof(magic));
        if (magic == ESP_APP_DESC_MAGIC_WORD) {
            memcpy(desc, buf + i, sizeof(esp_app_desc_t));
            return true;
        }
    }
    return false;
}

static bool check_version(const char *fw_url, uint8_t gen)
{
    uint8_t cm, cn, cp;
    if (!parse_version(esp_app_get_description()->version, &cm, &cn, &cp)) {
        ESP_LOGW(TAG, "Cannot parse current version, proceeding");
        return true;
    }
    ESP_LOGI(TAG, "Current version: %d.%d.%d", cm, cn, cp);

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) return false;

    esp_http_client_config_t cfg = {
        .url = fw_url,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = false,
        .buffer_size = 1024,
#ifdef CONFIG_BLE_SRV_OTA_URL_SKIP_CERT_CHECK
        .skip_cert_common_name_check = true,
#endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed");
        return true;
    }

    esp_http_client_set_header(client, "Range", "bytes=0-4095");

    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed");
        esp_http_client_cleanup(client);
        return true;
    }

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    esp_http_client_fetch_headers(client);
    int sc = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Version check HTTP %d", sc);

    if (sc != 200 && sc != 206) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    uint8_t *hdr = heap_caps_malloc(FW_HEADER_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hdr) hdr = malloc(FW_HEADER_BUF_SIZE);
    if (!hdr) {
        ESP_LOGE(TAG, "Header alloc failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    int total = 0;
    while (total < FW_HEADER_BUF_SIZE && !ble_srv_ota_is_abort_requested() && ble_srv_ota_gen_valid(gen)) {
        int r = esp_http_client_read(client, (char *)(hdr + total), FW_HEADER_BUF_SIZE - total);
        if (r <= 0) break;
        total += r;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        free(hdr);
        return false;
    }

    bool proceed = true;
    if (total >= (int)sizeof(esp_app_desc_t)) {
        esp_app_desc_t rd;
        if (find_app_desc(hdr, total, &rd)) {
            ESP_LOGI(TAG, "Remote firmware: version=%s", rd.version);
            uint8_t nm, nn, np;
            if (parse_version(rd.version, &nm, &nn, &np)) {
                proceed = version_newer(cm, cn, cp, nm, nn, np);
                ESP_LOGI(TAG, "Remote: %d.%d.%d -> %s", nm, nn, np, proceed ? "update needed" : "up to date");
            }
        }
    }
    free(hdr);
    return proceed;
}

static void url_task_finish(uint8_t gen, ble_ota_state_t result, ble_ota_err_t error)
{
    s_url_task = NULL;
    ble_srv_ota_finish(gen, result, error);
    vTaskDelete(NULL);
}

static void url_task(void *arg)
{
    (void)arg;
    uint8_t gen = (uint8_t)(uintptr_t)arg;
    s_url_task = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "URL OTA task started: %s, gen=%u", s_ota_url, gen);

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_CHECKING, BLE_OTA_ERR_NONE);

    bool need_update = check_version(s_ota_url, gen);

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        url_task_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
        return;
    }

    if (!need_update) {
        url_task_finish(gen, BLE_OTA_STATE_CHECK_FAIL, BLE_OTA_ERR_NONE);
        return;
    }

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_CHECK_OK, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "Downloading: %s", s_ota_url);

    esp_http_client_config_t http_cfg = {
        .url = s_ota_url,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = false,
        .buffer_size = HTTP_BUFFER_SIZE,
#ifdef CONFIG_BLE_SRV_OTA_URL_SKIP_CERT_CHECK
        .skip_cert_common_name_check = true,
#endif
    };

    esp_https_ota_config_t ota_cfg = { .http_config = &http_cfg };

    esp_https_ota_handle_t h = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_cfg, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "https_ota_begin failed: %s", esp_err_to_name(ret));
        url_task_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return;
    }

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        esp_https_ota_abort(h);
        url_task_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
        return;
    }

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_RECEIVING, BLE_OTA_ERR_NONE);

    int last_pct = -1;
    bool aborted = false;

    while (1) {
        if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
            if (!ble_srv_ota_gen_valid(gen)) {
                ESP_LOGW(TAG, "URL OTA gen invalid, aborting");
            } else {
                ESP_LOGW(TAG, "URL OTA abort requested");
            }
            esp_https_ota_abort(h);
            aborted = true;
            break;
        }

        ret = esp_https_ota_perform(h);

        if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
            ESP_LOGW(TAG, "URL OTA abort after perform");
            esp_https_ota_abort(h);
            aborted = true;
            break;
        }

        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int total_bytes = esp_https_ota_get_image_len_read(h);
            int size = esp_https_ota_get_image_size(h);
            if (size > 0) {
                ble_srv_ota_set_fw_size(gen, size);
                int pct = (total_bytes * 100) / size;
                if (pct != last_pct) {
                    ble_srv_ota_report_progress(gen, total_bytes, total_bytes);
                    last_pct = pct;
                    ble_srv_ota_push_status(gen);
                    if (pct % 10 == 0) {
                        ESP_LOGI(TAG, "URL OTA: %d%% (%d/%d)", pct, total_bytes, size);
                    }
                }
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOOP_TICK_MS));
        } else {
            break;
        }
    }

    if (aborted) {
        url_task_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
        return;
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Download complete, verifying...");
        ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFYING, BLE_OTA_ERR_NONE);

        ret = esp_https_ota_finish(h);
        if (ret == ESP_OK) {
            ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFY_OK, BLE_OTA_ERR_NONE);
            ESP_LOGI(TAG, "Verify OK, setting boot partition");

            const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
            if (target) {
                ble_srv_ota_set_state(gen, BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);
                ret = esp_ota_set_boot_partition(target);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Apply OK");
                    url_task_finish(gen, BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
                    return;
                } else {
                    ESP_LOGE(TAG, "Set boot failed: %s", esp_err_to_name(ret));
                    url_task_finish(gen, BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_INTERNAL);
                    return;
                }
            } else {
                url_task_finish(gen, BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_NO_PARTITION);
                return;
            }
        } else {
            ESP_LOGE(TAG, "Finish failed: %s", esp_err_to_name(ret));
            url_task_finish(gen, BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_VERIFY_FAILED);
            return;
        }
    } else {
        ESP_LOGE(TAG, "Download failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(h);
        url_task_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return;
    }
}

bool ble_srv_ota_url_init(void)
{
    s_url_task = NULL;
    memset(s_ota_url, 0, sizeof(s_ota_url));
    return true;
}

void ble_srv_ota_url_deinit(void)
{
    if (s_url_task) {
        TaskHandle_t task = s_url_task;
        s_url_task = NULL;
        ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
        int wait = 0;
        while (eTaskGetState(task) != eDeleted && wait < 60) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait++;
        }
        if (eTaskGetState(task) != eDeleted) {
            vTaskDelete(task);
        }
    }
}

bool ble_srv_ota_url_start(const char *url)
{
    if (!url || strlen(url) == 0) {
        ESP_LOGE(TAG, "Invalid URL");
        return false;
    }

    if (!ble_srv_wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected, cannot start URL OTA");
        return false;
    }

    if (s_url_task) {
        ESP_LOGW(TAG, "URL OTA task already running");
        return false;
    }

    uint8_t gen = ble_srv_ota_begin(BLE_OTA_MODE_URL);
    if (gen == BLE_OTA_INVALID_GEN) {
        ESP_LOGE(TAG, "Cannot begin URL OTA, session busy");
        return false;
    }

    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);
    s_ota_url[sizeof(s_ota_url) - 1] = '\0';

    BaseType_t ok = xTaskCreate(url_task, "ota_url", OTA_URL_TASK_STACK,
                                 (void *)(uintptr_t)gen,
                                 OTA_URL_TASK_PRIO, &s_url_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create URL OTA task");
        s_url_task = NULL;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    ESP_LOGI(TAG, "URL OTA started: %s, gen=%u", url, gen);
    return true;
}

void ble_srv_ota_url_handle_abort(void)
{
    if (ble_srv_ota_get_mode() != BLE_OTA_MODE_URL) {
        return;
    }
    if (s_url_task) {
        xTaskNotifyGive(s_url_task);
    }
}
