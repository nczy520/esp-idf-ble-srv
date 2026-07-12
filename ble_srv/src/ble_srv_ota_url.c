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
#define HTTP_TIMEOUT_MS       10000
#define HTTP_BUFFER_SIZE      4096
#define LOOP_TICK_MS          50

static TaskHandle_t s_url_task = NULL;
static char s_ota_url[BLE_OTA_URL_MAX_URL_LEN + 1] = {0};
static bool s_initialized = false;

#define FW_VER_MAX_SEGS 4
#define FW_VER_SEG_MAX  65535u

typedef struct {
    uint16_t segs[FW_VER_MAX_SEGS];
    uint8_t  nsegs;
    bool     valid;
} fw_ver_t;

static void fw_ver_parse(const char *str, fw_ver_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!str) return;

    const char *p = str;

    while (*p && (*p < '0' || *p > '9')) {
        p++;
    }
    if (!*p) return;

    uint8_t seg_idx = 0;
    while (seg_idx < FW_VER_MAX_SEGS) {
        uint32_t val = 0;
        bool has_digit = false;
        while (*p >= '0' && *p <= '9') {
            has_digit = true;
            uint32_t digit = (uint32_t)(*p - '0');
            if (val > (FW_VER_SEG_MAX - digit) / 10) {
                val = FW_VER_SEG_MAX;
            } else {
                val = val * 10 + digit;
            }
            p++;
        }
        if (!has_digit) break;
        out->segs[seg_idx] = (val > FW_VER_SEG_MAX) ? FW_VER_SEG_MAX : (uint16_t)val;
        seg_idx++;

        if (*p == '.') {
            p++;
            if (*p < '0' || *p > '9') break;
        } else {
            break;
        }
    }

    out->nsegs = seg_idx;
    out->valid = (seg_idx > 0);
}

static int fw_ver_compare(const fw_ver_t *a, const fw_ver_t *b)
{
    uint8_t max_segs = (a->nsegs > b->nsegs) ? a->nsegs : b->nsegs;
    for (uint8_t i = 0; i < max_segs; i++) {
        uint16_t av = (i < a->nsegs) ? a->segs[i] : 0;
        uint16_t bv = (i < b->nsegs) ? b->segs[i] : 0;
        if (av > bv) return 1;
        if (av < bv) return -1;
    }
    return 0;
}

static void fw_ver_to_string(const fw_ver_t *v, char *buf, size_t buf_len)
{
    if (!v->valid || buf_len == 0) {
        if (buf_len > 0) buf[0] = '\0';
        return;
    }
    size_t pos = 0;
    for (uint8_t i = 0; i < v->nsegs && pos < buf_len - 1; i++) {
        int written = snprintf(buf + pos, buf_len - pos, "%s%u",
                               (i == 0) ? "" : ".", v->segs[i]);
        if (written > 0) pos += (size_t)written;
        if (pos >= buf_len - 1) break;
    }
    buf[pos] = '\0';
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

typedef enum {
    VERSION_CHECK_ABORT = -1,
    VERSION_CHECK_PROCEED = 1,
    VERSION_CHECK_SKIP = 2,
    VERSION_CHECK_UP_TO_DATE = 3,
    VERSION_CHECK_DOWNGRADE = 4,
} version_check_result_t;

static version_check_result_t check_version(const char *fw_url, uint8_t gen)
{
    fw_ver_t cur_ver;
    fw_ver_parse(esp_app_get_description()->version, &cur_ver);
    if (!cur_ver.valid) {
        ESP_LOGW(TAG, "Cannot parse current version '%s', skipping version check",
                 esp_app_get_description()->version);
        return VERSION_CHECK_SKIP;
    }

    char cur_str[32];
    fw_ver_to_string(&cur_ver, cur_str, sizeof(cur_str));
    ESP_LOGI(TAG, "Current version: %s (parsed %u segments)", cur_str, cur_ver.nsegs);

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) return VERSION_CHECK_ABORT;

    esp_http_client_config_t cfg = {
        .url = fw_url,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .keep_alive_enable = false,
        .buffer_size = FW_HEADER_BUF_SIZE,
        .buffer_size_tx = 1024,
#ifdef CONFIG_BLE_SRV_OTA_URL_SKIP_CERT_CHECK
        .skip_cert_common_name_check = true,
#endif
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "HTTP client init failed, skipping version check");
        return VERSION_CHECK_SKIP;
    }

    esp_http_client_set_header(client, "Range", "bytes=0-4095");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP open failed: %s (0x%x), skipping version check", esp_err_to_name(err), err);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_ABORT;
    }

    int64_t fetch_ret = esp_http_client_fetch_headers(client);
    if (fetch_ret < 0) {
        ESP_LOGW(TAG, "HTTP fetch headers failed: %s (%lld), skipping version check",
                 esp_err_to_name((esp_err_t)fetch_ret), (long long)fetch_ret);
        int64_t clen = esp_http_client_get_content_length(client);
        ESP_LOGW(TAG, "  content_length=%lld", (long long)clen);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    int sc = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "Version check HTTP %d, content_length=%lld", sc,
             (long long)esp_http_client_get_content_length(client));

    if (sc == 404 || sc == 416 || sc >= 500) {
        ESP_LOGW(TAG, "HTTP server error: %d, skipping version check", sc);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    if (sc != 200 && sc != 206) {
        ESP_LOGW(TAG, "Unexpected HTTP status: %d, skipping version check", sc);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    uint8_t *hdr = heap_caps_malloc(FW_HEADER_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hdr) hdr = heap_caps_malloc(FW_HEADER_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!hdr) {
        ESP_LOGW(TAG, "Header alloc failed, skipping version check");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    int total = 0;
    int zero_read_count = 0;
    while (total < FW_HEADER_BUF_SIZE && !ble_srv_ota_is_abort_requested() && ble_srv_ota_gen_valid(gen)) {
        int r = esp_http_client_read(client, (char *)(hdr + total), FW_HEADER_BUF_SIZE - total);
        if (r < 0) {
            ESP_LOGW(TAG, "HTTP read error: %d, got %d bytes", r, total);
            break;
        }
        if (r == 0) {
            zero_read_count++;
            if (zero_read_count >= 50) {
                ESP_LOGW(TAG, "HTTP read stalled after %d bytes", total);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        zero_read_count = 0;
        total += r;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        heap_caps_free(hdr);
        return VERSION_CHECK_ABORT;
    }

    if (total < (int)sizeof(esp_app_desc_t)) {
        ESP_LOGW(TAG, "Header too short (%d bytes), cannot verify version, proceeding", total);
        heap_caps_free(hdr);
        return VERSION_CHECK_SKIP;
    }

    esp_app_desc_t rd;
    if (!find_app_desc(hdr, total, &rd)) {
        ESP_LOGW(TAG, "Could not find app descriptor in header, skipping version check");
        heap_caps_free(hdr);
        return VERSION_CHECK_SKIP;
    }

    ESP_LOGI(TAG, "Remote firmware: version='%s'", rd.version);

    fw_ver_t rem_ver;
    fw_ver_parse(rd.version, &rem_ver);
    heap_caps_free(hdr);

    if (!rem_ver.valid) {
        ESP_LOGW(TAG, "Cannot parse remote version '%s', proceeding with download", rd.version);
        return VERSION_CHECK_SKIP;
    }

    char rem_str[32];
    fw_ver_to_string(&rem_ver, rem_str, sizeof(rem_str));
    ESP_LOGI(TAG, "Remote version parsed: %s (%u segments)", rem_str, rem_ver.nsegs);

    int cmp = fw_ver_compare(&rem_ver, &cur_ver);
    if (cmp > 0) {
        ESP_LOGI(TAG, "Remote version %s > current %s, update needed", rem_str, cur_str);
        return VERSION_CHECK_PROCEED;
    } else if (cmp == 0) {
        ESP_LOGI(TAG, "Remote version %s == current %s, already up to date", rem_str, cur_str);
        return VERSION_CHECK_UP_TO_DATE;
    } else {
        ESP_LOGW(TAG, "Remote version %s < current %s, downgrade rejected", rem_str, cur_str);
        return VERSION_CHECK_DOWNGRADE;
    }
}

static void url_task_finish(uint8_t gen, ble_ota_state_t result, ble_ota_err_t error)
{
    s_url_task = NULL;
    ble_srv_ota_finish(gen, result, error);
    vTaskDelete(NULL);
}

static void url_task(void *arg)
{
    uint8_t gen = (uint8_t)(uintptr_t)arg;
    s_url_task = xTaskGetCurrentTaskHandle();

    ESP_LOGI(TAG, "URL OTA task started: %s, gen=%u", s_ota_url, gen);

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_CHECKING, BLE_OTA_ERR_NONE);

    version_check_result_t check_res = check_version(s_ota_url, gen);

    if (check_res == VERSION_CHECK_ABORT || ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        url_task_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
        return;
    }

    if (check_res == VERSION_CHECK_DOWNGRADE) {
        ESP_LOGW(TAG, "Remote firmware is older than current, rejecting downgrade");
        url_task_finish(gen, BLE_OTA_STATE_CHECK_FAIL, BLE_OTA_ERR_VERSION_DOWNGRADE);
        return;
    }

    if (check_res == VERSION_CHECK_UP_TO_DATE) {
        ESP_LOGI(TAG, "Firmware is already up to date, no update needed");
        url_task_finish(gen, BLE_OTA_STATE_CHECK_FAIL, BLE_OTA_ERR_VERSION_SAME);
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
            ble_srv_ota_set_state(gen, BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);
            ESP_LOGI(TAG, "Verify and apply OK");
            url_task_finish(gen, BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
            return;
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
    if (s_initialized) {
        ESP_LOGW(TAG, "URL OTA already initialized");
        return true;
    }
    s_url_task = NULL;
    memset(s_ota_url, 0, sizeof(s_ota_url));
    s_initialized = true;
    return true;
}

void ble_srv_ota_url_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    if (s_url_task) {
        TaskHandle_t task = s_url_task;
        ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
        xTaskNotifyGive(task);
        s_url_task = NULL;
        int wait = 0;
        while (eTaskGetState(task) != eDeleted && wait < 100) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wait++;
        }
        if (eTaskGetState(task) != eDeleted) {
            ESP_LOGW(TAG, "URL OTA task did not exit in time, force deleting");
            vTaskDelete(task);
        }
    }
    s_initialized = false;
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
