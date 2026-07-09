#include "ble_srv_ota_common.h"
#include "ble_srv_ota_url.h"
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

static const char *TAG = "BLE_SRV_OTA_URL";

#define OTA_URL_TASK_STACK    8192
#define OTA_URL_TASK_PRIO     5
#define FW_HEADER_BUF_SIZE    4096

static TaskHandle_t s_url_task = NULL;
static volatile bool s_url_running = false;
static char s_ota_url[BLE_OTA_URL_MAX_URL_LEN + 1] = {0};

static bool parse_version_string(const char *ver_str, uint8_t *major, uint8_t *minor, uint8_t *patch)
{
    if (!ver_str || !major || !minor || !patch) {
        return false;
    }
    int m = 0, n = 0, p = 0;
    if (sscanf(ver_str, "%d.%d.%d", &m, &n, &p) != 3) {
        return false;
    }
    *major = (uint8_t)m;
    *minor = (uint8_t)n;
    *patch = (uint8_t)p;
    return true;
}

static bool get_current_version(uint8_t *major, uint8_t *minor, uint8_t *patch)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    return parse_version_string(desc->version, major, minor, patch);
}

static bool compare_version(uint8_t cur_major, uint8_t cur_minor, uint8_t cur_patch,
                            uint8_t new_major, uint8_t new_minor, uint8_t new_patch)
{
    if (new_major > cur_major) return true;
    if (new_major < cur_major) return false;
    if (new_minor > cur_minor) return true;
    if (new_minor < cur_minor) return false;
    if (new_patch > cur_patch) return true;
    return false;
}

static bool find_app_desc_in_header(const uint8_t *buf, size_t buf_len, esp_app_desc_t *desc)
{
    if (!buf || !desc || buf_len < sizeof(esp_app_desc_t)) {
        return false;
    }

    for (size_t i = 0; i <= buf_len - sizeof(esp_app_desc_t); i += 4) {
        uint32_t magic = *(uint32_t *)(buf + i);
        if (magic == ESP_APP_DESC_MAGIC_WORD) {
            memcpy(desc, buf + i, sizeof(esp_app_desc_t));
            return true;
        }
    }

    return false;
}

static bool check_version_from_header(const char *fw_url)
{
    if (!ble_srv_wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected, cannot perform URL OTA");
        ble_srv_ota_set_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_NETWORK);
        ble_srv_ota_push_status();
        return false;
    }

    uint8_t cur_major, cur_minor, cur_patch;
    if (!get_current_version(&cur_major, &cur_minor, &cur_patch)) {
        ESP_LOGW(TAG, "Cannot parse current version, proceeding with OTA");
        return true;
    }
    ESP_LOGI(TAG, "Current version: %d.%d.%d", cur_major, cur_minor, cur_patch);

    esp_http_client_config_t http_config = {
        .url = fw_url,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for version check");
        return true;
    }

    esp_http_client_set_header(client, "Range", "bytes=0-4095");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open URL for version check: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return true;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Version check HTTP %d, content_length=%d", status_code, content_length);

    if (status_code != 200 && status_code != 206) {
        ESP_LOGW(TAG, "HTTP error %d, proceeding with OTA without version check", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    uint8_t *header_buf = malloc(FW_HEADER_BUF_SIZE);
    if (!header_buf) {
        ESP_LOGE(TAG, "Failed to allocate header buffer");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return true;
    }

    int total_read = 0;
    while (total_read < FW_HEADER_BUF_SIZE) {
        int read_len = esp_http_client_read(client, (char *)(header_buf + total_read),
                                             FW_HEADER_BUF_SIZE - total_read);
        if (read_len <= 0) {
            break;
        }
        total_read += read_len;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (total_read < sizeof(esp_app_desc_t)) {
        ESP_LOGW(TAG, "Header too small (%d bytes), proceeding with OTA", total_read);
        free(header_buf);
        return true;
    }

    esp_app_desc_t remote_desc;
    if (!find_app_desc_in_header(header_buf, total_read, &remote_desc)) {
        ESP_LOGW(TAG, "esp_app_desc_t not found in header, proceeding with OTA");
        free(header_buf);
        return true;
    }
    free(header_buf);

    ESP_LOGI(TAG, "Remote firmware: version=%s, project=%s",
             remote_desc.version, remote_desc.project_name);

    uint8_t new_major, new_minor, new_patch;
    if (!parse_version_string(remote_desc.version, &new_major, &new_minor, &new_patch)) {
        ESP_LOGW(TAG, "Cannot parse remote version '%s', proceeding with OTA", remote_desc.version);
        return true;
    }
    ESP_LOGI(TAG, "Remote version: %d.%d.%d", new_major, new_minor, new_patch);

    bool need_update = compare_version(cur_major, cur_minor, cur_patch,
                                        new_major, new_minor, new_patch);

    if (need_update) {
        ESP_LOGI(TAG, "New version available: %d.%d.%d > %d.%d.%d",
                 new_major, new_minor, new_patch, cur_major, cur_minor, cur_patch);
    } else {
        ESP_LOGI(TAG, "Already up to date: %d.%d.%d >= %d.%d.%d",
                 cur_major, cur_minor, cur_patch, new_major, new_minor, new_patch);
    }

    return need_update;
}

static void ble_srv_ota_url_task(void *arg)
{
    ESP_LOGI(TAG, "URL OTA task started, URL: %s", s_ota_url);

    ble_srv_ota_set_state(BLE_OTA_STATE_CHECKING, BLE_OTA_ERR_NONE);

    bool need_update = check_version_from_header(s_ota_url);

    if (!need_update) {
        ble_srv_ota_set_state(BLE_OTA_STATE_CHECK_FAIL, BLE_OTA_ERR_NONE);
        ESP_LOGI(TAG, "No update needed, firmware is up to date");
        s_url_running = false;
        s_url_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    ble_srv_ota_set_state(BLE_OTA_STATE_CHECK_OK, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "Downloading firmware: %s", s_ota_url);

    esp_http_client_config_t http_config = {
        .url = s_ota_url,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = false,
        .skip_cert_common_name_check = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    ble_srv_ota_set_state(BLE_OTA_STATE_RECEIVING, BLE_OTA_ERR_NONE);

    esp_https_ota_handle_t https_ota_handle = NULL;
    esp_err_t ret = esp_https_ota_begin(&ota_config, &https_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "URL OTA begin failed: %s", esp_err_to_name(ret));
        ble_srv_ota_set_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        s_url_running = false;
        s_url_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    int last_pct = -1;
    while (1) {
        ret = esp_https_ota_perform(https_ota_handle);
        if (ret == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int total = esp_https_ota_get_image_len_read(https_ota_handle);
            int size = esp_https_ota_get_image_size(https_ota_handle);
            if (size > 0) {
                int pct = (total * 100) / size;
                if (pct != last_pct) {
                    ble_srv_ota_update_progress(size, total);
                    last_pct = pct;
                    ble_srv_ota_push_status();
                    if (pct % 10 == 0) {
                        ESP_LOGI(TAG, "URL OTA: %d%% (%d/%d)", pct, total, size);
                    }
                }
            }
        } else {
            break;
        }

        if (!s_url_running) {
            ESP_LOGW(TAG, "URL OTA aborted by user");
            esp_https_ota_abort(https_ota_handle);
            ble_srv_ota_set_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
            s_url_task = NULL;
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "URL OTA download complete, verifying...");
        ble_srv_ota_set_state(BLE_OTA_STATE_VERIFYING, BLE_OTA_ERR_NONE);

        ret = esp_https_ota_finish(https_ota_handle);
        if (ret == ESP_OK) {
            ble_srv_ota_set_state(BLE_OTA_STATE_VERIFY_OK, BLE_OTA_ERR_NONE);
            ESP_LOGI(TAG, "URL OTA verify OK, applying...");

            const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
            if (target) {
                ble_srv_ota_set_state(BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);
                ret = esp_ota_set_boot_partition(target);
                if (ret == ESP_OK) {
                    ble_srv_ota_set_state(BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
                    ESP_LOGI(TAG, "URL OTA complete! Notify sent, rebooting in 3s...");
                    vTaskDelay(pdMS_TO_TICKS(3000));
                    esp_restart();
                } else {
                    ESP_LOGE(TAG, "Set boot partition failed: %s", esp_err_to_name(ret));
                    ble_srv_ota_set_state(BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_INTERNAL);
                }
            } else {
                ble_srv_ota_set_state(BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_NO_PARTITION);
            }
        } else {
            ESP_LOGE(TAG, "URL OTA finish failed: %s", esp_err_to_name(ret));
            ble_srv_ota_set_state(BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_VERIFY_FAILED);
        }
    } else {
        ESP_LOGE(TAG, "URL OTA perform failed: %s", esp_err_to_name(ret));
        esp_https_ota_abort(https_ota_handle);
        ble_srv_ota_set_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
    }

    s_url_running = false;
    s_url_task = NULL;
    vTaskDelete(NULL);
}

bool ble_srv_ota_url_init(void)
{
    return true;
}

void ble_srv_ota_url_deinit(void)
{
    ble_srv_ota_url_abort();
}

bool ble_srv_ota_url_start(const char *url)
{
    if (!url || strlen(url) == 0) {
        ESP_LOGE(TAG, "Invalid URL");
        return false;
    }

    if (!ble_srv_wifi_is_connected()) {
        ESP_LOGE(TAG, "WiFi not connected, cannot start URL OTA");
        ble_srv_ota_set_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_NETWORK);
        ble_srv_ota_push_status();
        return false;
    }

    if (s_url_running) {
        ESP_LOGW(TAG, "URL OTA already running");
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress (state=%d)", ble_srv_ota_get_state());
        return false;
    }

    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);
    s_ota_url[sizeof(s_ota_url) - 1] = '\0';

    s_url_running = true;

    BaseType_t ok = xTaskCreate(ble_srv_ota_url_task, "ota_url",
                                OTA_URL_TASK_STACK, NULL,
                                OTA_URL_TASK_PRIO, &s_url_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create URL OTA task");
        s_url_running = false;
        return false;
    }

    ESP_LOGI(TAG, "URL OTA started: %s", url);
    return true;
}

bool ble_srv_ota_url_start_default(void)
{
#ifdef CONFIG_BLE_SRV_OTA_URL_DEFAULT
    const char *default_url = CONFIG_BLE_SRV_OTA_URL_DEFAULT;
    if (strlen(default_url) == 0) {
        ESP_LOGE(TAG, "Default OTA URL is empty");
        return false;
    }
    ESP_LOGI(TAG, "Starting URL OTA with default URL: %s", default_url);
    return ble_srv_ota_url_start(default_url);
#else
    ESP_LOGE(TAG, "No default OTA URL configured");
    return false;
#endif
}

void ble_srv_ota_url_abort(void)
{
    if (!s_url_running) {
        return;
    }

    s_url_running = false;

    if (s_url_task) {
        for (int i = 0; i < 30; i++) {
            eTaskState ts = eTaskGetState(s_url_task);
            if (ts == eDeleted || ts == eInvalid) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        eTaskState ts = eTaskGetState(s_url_task);
        if (ts != eDeleted && ts != eInvalid) {
            ESP_LOGW(TAG, "URL OTA task still running, forcing delete");
            vTaskDelete(s_url_task);
        }
        s_url_task = NULL;
    }

    ble_srv_ota_set_state(BLE_OTA_STATE_IDLE, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "URL OTA aborted");
}

bool ble_srv_ota_url_is_running(void)
{
    return s_url_running;
}
