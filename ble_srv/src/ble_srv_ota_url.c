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

#define OTA_LOGI(...) do { ESP_LOGI(TAG, __VA_ARGS__); ble_srv_gatt_log_send(BLE_SRV_LOG_LEVEL_INFO,  TAG, __VA_ARGS__); } while(0)
#define OTA_LOGW(...) do { ESP_LOGW(TAG, __VA_ARGS__); ble_srv_gatt_log_send(BLE_SRV_LOG_LEVEL_WARN,  TAG, __VA_ARGS__); } while(0)
#define OTA_LOGE(...) do { ESP_LOGE(TAG, __VA_ARGS__); ble_srv_gatt_log_send(BLE_SRV_LOG_LEVEL_ERROR, TAG, __VA_ARGS__); } while(0)

static const char *TAG = "OTA_URL";

#define OTA_URL_TASK_STACK    8192
#define OTA_URL_TASK_PRIO     5
#define FW_HEADER_BUF_SIZE    4096
#define HTTP_TIMEOUT_MS       10000
#define HTTP_BUFFER_SIZE      4096
#define LOOP_TICK_MS          50
#define HTTP_READ_STALL_COUNT 50
#define HTTP_READ_TICK_MS     20
#define URL_DEINIT_WAIT_MS    1000
#define URL_DEINIT_POLL_MS    10
#define HTTP_RANGE_BYTES      4095

static volatile TaskHandle_t s_url_task = NULL;
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
        OTA_LOGW("Cannot parse current version '%s', skipping version check",
                 esp_app_get_description()->version);
        return VERSION_CHECK_SKIP;
    }

    char cur_str[32];
    fw_ver_to_string(&cur_ver, cur_str, sizeof(cur_str));
    OTA_LOGI("Current version: %s (parsed %u segments)", cur_str, cur_ver.nsegs);

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
        OTA_LOGE("HTTP client init failed, skipping version check");
        return VERSION_CHECK_SKIP;
    }

    char range_header[32];
    snprintf(range_header, sizeof(range_header), "bytes=0-%u", HTTP_RANGE_BYTES);
    esp_http_client_set_header(client, "Range", range_header);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        OTA_LOGW("HTTP open failed: %s (0x%x), skipping version check", esp_err_to_name(err), err);
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
        OTA_LOGW("HTTP fetch headers failed: %s (%lld), skipping version check",
                 esp_err_to_name((esp_err_t)fetch_ret), (long long)fetch_ret);
        int64_t clen = esp_http_client_get_content_length(client);
        OTA_LOGW("  content_length=%lld", (long long)clen);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    int sc = esp_http_client_get_status_code(client);
    int64_t clen = esp_http_client_get_content_length(client);
    OTA_LOGI("Version check HTTP %d, content_length=%lld", sc, (long long)clen);

    if (sc == 404) {
        OTA_LOGE("HTTP 404 Not Found: firmware file does not exist at URL");
        OTA_LOGE("  URL: %s", fw_url);
        OTA_LOGE("  Check that the firmware file is uploaded and the URL is correct");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }
    if (sc == 416) {
        OTA_LOGE("HTTP 416 Range Not Satisfiable: server does not support Range requests");
        OTA_LOGE("  Expected range: bytes=0-%u", HTTP_RANGE_BYTES);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }
    if (sc == 401 || sc == 403) {
        OTA_LOGE("HTTP %d %s: authentication required or access denied",
                 sc, (sc == 401) ? "Unauthorized" : "Forbidden");
        OTA_LOGE("  The firmware URL requires authentication, which is not supported");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }
    if (sc >= 500 && sc < 600) {
        OTA_LOGE("HTTP %d Server Error: server is unable to serve the firmware", sc);
        OTA_LOGE("  URL: %s", fw_url);
        OTA_LOGE("  This may be temporary, try again later");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }
    if (sc >= 300 && sc < 400) {
        OTA_LOGE("HTTP %d Redirect: redirects are not followed for version check", sc);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }
    if (sc != 200 && sc != 206) {
        OTA_LOGE("Unexpected HTTP status: %d", sc);
        OTA_LOGE("  Expected 200 (OK) or 206 (Partial Content)");
        OTA_LOGE("  URL: %s", fw_url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    uint8_t *hdr = heap_caps_malloc(FW_HEADER_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!hdr) hdr = heap_caps_malloc(FW_HEADER_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!hdr) {
        OTA_LOGW("Header alloc failed, skipping version check");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return VERSION_CHECK_SKIP;
    }

    int total = 0;
    int zero_read_count = 0;
    while (total < FW_HEADER_BUF_SIZE && !ble_srv_ota_is_abort_requested() && ble_srv_ota_gen_valid(gen)) {
        int r = esp_http_client_read(client, (char *)(hdr + total), FW_HEADER_BUF_SIZE - total);
        if (r < 0) {
            OTA_LOGW("HTTP read error: %d, got %d bytes", r, total);
            break;
        }
        if (r == 0) {
            zero_read_count++;
            if (zero_read_count >= HTTP_READ_STALL_COUNT) {
                OTA_LOGW("HTTP read stalled after %d bytes", total);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(HTTP_READ_TICK_MS));
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

    if (total < (int)sizeof(esp_image_header_t)) {
        OTA_LOGE("HTTP response too short: only %d bytes (need at least %u for image header)",
                 total, (unsigned)sizeof(esp_image_header_t));
        OTA_LOGE("  The URL may not be pointing to a valid ESP32 firmware binary");
        OTA_LOGE("  Check URL: %s", fw_url);
        heap_caps_free(hdr);
        return VERSION_CHECK_SKIP;
    }

    esp_image_header_t img_hdr;
    memcpy(&img_hdr, hdr, sizeof(img_hdr));
    if (img_hdr.magic != ESP_IMAGE_HEADER_MAGIC) {
        OTA_LOGE("Invalid firmware: bad magic byte 0x%02x (expected 0x%02x)",
                 img_hdr.magic, ESP_IMAGE_HEADER_MAGIC);
        OTA_LOGE("  The file at URL is not a valid ESP32 firmware image");
        OTA_LOGE("  First 16 bytes: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                 hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7],
                 hdr[8], hdr[9], hdr[10], hdr[11], hdr[12], hdr[13], hdr[14], hdr[15]);
        heap_caps_free(hdr);
        return VERSION_CHECK_SKIP;
    }

    OTA_LOGI("Image header valid: magic=0x%02x, segments=%u, entry=0x%08lx, spi_mode=%u",
             img_hdr.magic, img_hdr.segment_count, (unsigned long)img_hdr.entry_addr, img_hdr.spi_mode);

    if (total < (int)sizeof(esp_app_desc_t)) {
        OTA_LOGW("Header too short for app descriptor (%d bytes), cannot verify version, proceeding anyway", total);
        heap_caps_free(hdr);
        return VERSION_CHECK_SKIP;
    }

    esp_app_desc_t rd;
    if (!find_app_desc(hdr, total, &rd)) {
        OTA_LOGE("Could not find app descriptor (magic word 0x%08x) in firmware header",
                 ESP_APP_DESC_MAGIC_WORD);
        OTA_LOGE("  Read %d bytes from URL but app_desc_t structure not found", total);
        OTA_LOGE("  This firmware may be corrupt or built for a different platform");
        heap_caps_free(hdr);
        return VERSION_CHECK_SKIP;
    }

    OTA_LOGI("Remote firmware info:");
    OTA_LOGI("  version    : %s", rd.version);
    OTA_LOGI("  project    : %s", rd.project_name);
    OTA_LOGI("  idf-ver    : %s", rd.idf_ver);
    OTA_LOGI("  secure-ver : %lu", (unsigned long)rd.secure_version);
    OTA_LOGI("  compiled   : %s %s", rd.date, rd.time);

    fw_ver_t rem_ver;
    fw_ver_parse(rd.version, &rem_ver);
    heap_caps_free(hdr);

    if (!rem_ver.valid) {
        OTA_LOGW("Cannot parse remote version '%s', proceeding with download", rd.version);
        return VERSION_CHECK_SKIP;
    }

    char rem_str[32];
    fw_ver_to_string(&rem_ver, rem_str, sizeof(rem_str));
    OTA_LOGI("Remote version parsed: %s (%u segments)", rem_str, rem_ver.nsegs);

    int cmp = fw_ver_compare(&rem_ver, &cur_ver);
    if (cmp > 0) {
        OTA_LOGI("Remote version %s > current %s, update needed", rem_str, cur_str);
        return VERSION_CHECK_PROCEED;
    } else if (cmp == 0) {
        OTA_LOGI("Remote version %s == current %s, already up to date", rem_str, cur_str);
        return VERSION_CHECK_UP_TO_DATE;
    } else {
        OTA_LOGW("Remote version %s < current %s, downgrade rejected", rem_str, cur_str);
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

    OTA_LOGI("URL OTA task started: %s, gen=%u", s_ota_url, gen);

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_CHECKING, BLE_OTA_ERR_NONE);

    version_check_result_t check_res = check_version(s_ota_url, gen);

    if (check_res == VERSION_CHECK_ABORT || ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
        url_task_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
        return;
    }

    if (check_res == VERSION_CHECK_DOWNGRADE) {
#ifdef CONFIG_BLE_SRV_OTA_URL_ALLOW_DOWNGRADE
        OTA_LOGW("Remote firmware is older than current, downgrade allowed");
#else
        OTA_LOGW("Remote firmware is older than current, rejecting downgrade");
        url_task_finish(gen, BLE_OTA_STATE_CHECK_FAIL, BLE_OTA_ERR_VERSION_DOWNGRADE);
        return;
#endif
    }

    if (check_res == VERSION_CHECK_UP_TO_DATE) {
        OTA_LOGI("Firmware is already up to date, no update needed");
        url_task_finish(gen, BLE_OTA_STATE_CHECK_FAIL, BLE_OTA_ERR_VERSION_SAME);
        return;
    }

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_CHECK_OK, BLE_OTA_ERR_NONE);
    OTA_LOGI("Downloading: %s", s_ota_url);

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
        OTA_LOGE("OTA begin failed: %s (0x%x)", esp_err_to_name(ret), ret);
        switch (ret) {
        case ESP_ERR_INVALID_ARG:
            OTA_LOGE("  Reason: invalid HTTP config or handle");
            break;
        case ESP_ERR_NO_MEM:
            OTA_LOGE("  Reason: out of memory for OTA buffer/partition");
            break;
        case ESP_ERR_OTA_BASE:
        case ESP_FAIL:
        default:
            OTA_LOGE("  Reason: HTTP connection failed or unable to open OTA partition");
            OTA_LOGE("  Check: URL is correct, server is reachable, WiFi is stable");
            break;
        }
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
                OTA_LOGW("URL OTA gen invalid, aborting");
            } else {
                OTA_LOGW("URL OTA abort requested");
            }
            esp_https_ota_abort(h);
            aborted = true;
            break;
        }

        ret = esp_https_ota_perform(h);

        if (ble_srv_ota_is_abort_requested() || !ble_srv_ota_gen_valid(gen)) {
            OTA_LOGW("URL OTA abort after perform");
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
                        OTA_LOGI("Download progress: %d%% (%d / %d bytes)", pct, total_bytes, size);
                    }
                }
            } else {
                if (total_bytes > 0 && total_bytes % (HTTP_BUFFER_SIZE * 4) < HTTP_BUFFER_SIZE) {
                    OTA_LOGI("Downloaded %d bytes (content-length unknown)", total_bytes);
                }
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LOOP_TICK_MS));
        } else {
            int downloaded = esp_https_ota_get_image_len_read(h);
            int total_size = esp_https_ota_get_image_size(h);
            OTA_LOGW("Download loop ended: ret=%s, downloaded=%d, total=%d",
                     esp_err_to_name(ret), downloaded, total_size);
            break;
        }
    }

    if (aborted) {
        url_task_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
        return;
    }

    if (ret == ESP_OK) {
        int total_bytes = esp_https_ota_get_image_len_read(h);
        OTA_LOGI("Download complete: %d bytes received, verifying firmware...", total_bytes);
        ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFYING, BLE_OTA_ERR_NONE);

        ret = esp_https_ota_finish(h);
        if (ret == ESP_OK) {
            ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFY_OK, BLE_OTA_ERR_NONE);
            ble_srv_ota_set_state(gen, BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);
            OTA_LOGI("Firmware verification and application successful!");
            OTA_LOGI("  New firmware will boot after restart");
            url_task_finish(gen, BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
            return;
        } else {
            OTA_LOGE("Firmware verification failed: %s (0x%x)", esp_err_to_name(ret), ret);
            switch (ret) {
            case ESP_ERR_OTA_VALIDATE_FAILED:
                OTA_LOGE("  Reason: firmware image validation failed");
                OTA_LOGE("  - Bad magic byte, invalid image header");
                OTA_LOGE("  - Checksum or SHA-256 hash mismatch");
                OTA_LOGE("  - Secure boot signature verification failed (if enabled)");
                OTA_LOGE("  - The downloaded file is truncated, corrupt, or not a valid ESP32 firmware");
                OTA_LOGE("  - Ensure the firmware is built for this chip target (ESP32-S3)");
                break;
            case ESP_ERR_OTA_PARTITION_CONFLICT:
                OTA_LOGE("  Reason: cannot write to currently running partition");
                break;
            case ESP_ERR_OTA_SELECT_INFO_INVALID:
                OTA_LOGE("  Reason: OTA data partition contains invalid content");
                OTA_LOGE("  - Partition table may be misconfigured");
                break;
            case ESP_ERR_OTA_SMALL_SEC_VER:
                OTA_LOGE("  Reason: new firmware has smaller secure version than current");
                OTA_LOGE("  - Anti-rollback protection prevents downgrade");
                break;
            case ESP_ERR_OTA_ROLLBACK_FAILED:
                OTA_LOGE("  Reason: rollback failed - no valid firmware in passive partition");
                break;
            case ESP_ERR_OTA_ROLLBACK_INVALID_STATE:
                OTA_LOGE("  Reason: current firmware is in PENDING_VERIFY state");
                OTA_LOGE("  - Current firmware has not been confirmed valid yet");
                OTA_LOGE("  - Call esp_ota_mark_app_valid_cancel_rollback() first");
                break;
            case ESP_ERR_INVALID_STATE:
                OTA_LOGE("  Reason: OTA handle in invalid state");
                break;
            case ESP_ERR_FLASH_BASE:
                OTA_LOGE("  Reason: flash write failed (hardware/partition issue)");
                break;
            default:
                OTA_LOGE("  Reason: unknown error (0x%x)", ret);
                break;
            }
            url_task_finish(gen, BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_VERIFY_FAILED);
            return;
        }
    } else {
        int downloaded = esp_https_ota_get_image_len_read(h);
        OTA_LOGE("Download failed: %s (0x%x), downloaded=%d bytes",
                 esp_err_to_name(ret), ret, downloaded);
        switch (ret) {
        case ESP_ERR_HTTP_CONNECT:
        case ESP_ERR_HTTP_CONNECTING:
            OTA_LOGE("  Reason: cannot connect to server");
            OTA_LOGE("  Check: WiFi signal strength, DNS resolution, server is up");
            break;
        case ESP_ERR_HTTP_READ_TIMEOUT:
            OTA_LOGE("  Reason: HTTP read timed out");
            OTA_LOGE("  Check: network stability, server responsiveness");
            break;
        case ESP_ERR_HTTP_INVALID_TRANSPORT:
        case ESP_ERR_HTTP_FETCH_HEADER:
            OTA_LOGE("  Reason: HTTP transport error or failed to fetch response headers");
            break;
        case ESP_ERR_HTTP_MAX_REDIRECT:
            OTA_LOGE("  Reason: too many HTTP redirects");
            OTA_LOGE("  URL: %s", s_ota_url);
            break;
        case ESP_ERR_HTTP_RANGE_NOT_SATISFIABLE:
            OTA_LOGE("  Reason: HTTP 416 Range Not Satisfiable");
            break;
        case ESP_ERR_HTTP_CONNECTION_CLOSED:
            OTA_LOGE("  Reason: server closed the connection prematurely");
            OTA_LOGE("  The firmware download was interrupted");
            break;
        case ESP_ERR_HTTP_INCOMPLETE_DATA:
            OTA_LOGE("  Reason: incomplete data received (less than Content-Length)");
            OTA_LOGE("  The firmware file may be truncated or network was interrupted");
            break;
        case ESP_ERR_HTTP_WRITE_DATA:
            OTA_LOGE("  Reason: failed to write HTTP data (OTA partition write error)");
            break;
        case ESP_ERR_NO_MEM:
            OTA_LOGE("  Reason: out of memory during download");
            break;
        default:
            OTA_LOGE("  Reason: network or protocol error (err=0x%x)", ret);
            break;
        }
        esp_https_ota_abort(h);
        url_task_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return;
    }
}

bool ble_srv_ota_url_init(void)
{
    if (s_initialized) {
        OTA_LOGW("URL OTA already initialized");
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
        int max_wait_ticks = URL_DEINIT_WAIT_MS / URL_DEINIT_POLL_MS;
        while (wait < max_wait_ticks) {
            vTaskDelay(pdMS_TO_TICKS(URL_DEINIT_POLL_MS));
            wait++;
        }
        OTA_LOGW("URL OTA task did not exit in time, force deleting");
        vTaskDelete(task);
    }
    s_initialized = false;
}

bool ble_srv_ota_url_start(const char *url)
{
    if (!url || strlen(url) == 0) {
        OTA_LOGE("Invalid URL");
        return false;
    }

    if (!ble_srv_wifi_is_connected()) {
        OTA_LOGE("WiFi not connected, cannot start URL OTA");
        return false;
    }

    if (s_url_task) {
        OTA_LOGW("URL OTA task already running");
        return false;
    }

    uint8_t gen = ble_srv_ota_begin(BLE_OTA_MODE_URL);
    if (gen == BLE_OTA_INVALID_GEN) {
        OTA_LOGE("Cannot begin URL OTA, session busy");
        return false;
    }

    strncpy(s_ota_url, url, sizeof(s_ota_url) - 1);
    s_ota_url[sizeof(s_ota_url) - 1] = '\0';

    TaskHandle_t task_handle = NULL;
    BaseType_t ok = xTaskCreate(url_task, "ota_url", OTA_URL_TASK_STACK,
                                 (void *)(uintptr_t)gen,
                                 OTA_URL_TASK_PRIO, &task_handle);
    if (ok != pdPASS) {
        OTA_LOGE("Failed to create URL OTA task");
        s_url_task = NULL;
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }
    s_url_task = task_handle;

    OTA_LOGI("URL OTA started: %s, gen=%u", url, gen);
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
