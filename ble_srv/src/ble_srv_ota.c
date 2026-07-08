#include "ble_srv.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"
#include "ble_srv_ota.h"

static const char *TAG = "BLE_SRV_OTA";

#define BLE_SRV_STREAM_BUF_SIZE     (64 * 1024)
#define BLE_SRV_STREAM_TRIGGER      (4 * 1024)
#define BLE_SRV_WRITER_TASK_STACK   6144
#define BLE_SRV_WRITER_TASK_PRIO    5

static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target_partition = NULL;
static uint32_t s_fw_total_size = 0;
static uint32_t s_fw_bytes_written = 0;
static ble_ota_state_t s_ota_state = BLE_OTA_STATE_IDLE;
static ble_ota_err_t s_ota_error = BLE_OTA_ERR_NONE;
static SemaphoreHandle_t s_state_lock = NULL;
static ble_srv_status_cb_t s_status_cb = NULL;

static StreamBufferHandle_t s_fw_stream = NULL;
static TaskHandle_t s_wr_task = NULL;
static volatile bool s_wr_running = false;
static volatile uint32_t s_total_received = 0;
static uint32_t s_last_status_update = 0;

extern uint16_t g_ota_status_chr_val_handle;
extern bool g_ota_status_notify_enabled;

extern uint16_t ble_srv_get_conn_handle(void);

static void ble_srv_set_ota_state(ble_ota_state_t state, ble_ota_err_t error);
static void ble_srv_writer_task(void *arg);
static bool ble_srv_process_ota_start(const uint8_t *data, uint16_t len);
static bool ble_srv_process_ota_verify(void);
static bool ble_srv_process_ota_apply(void);

bool ble_srv_ota_init(void)
{
    s_state_lock = xSemaphoreCreateMutex();
    if (!s_state_lock) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return false;
    }

    s_fw_stream = xStreamBufferCreate(BLE_SRV_STREAM_BUF_SIZE, BLE_SRV_STREAM_TRIGGER);
    if (!s_fw_stream) {
        ESP_LOGE(TAG, "Failed to create stream buffer");
        return false;
    }

    return true;
}

void ble_srv_ota_deinit(void)
{
    ble_srv_ota_reset();

    if (s_state_lock) {
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
    }

    if (s_fw_stream) {
        vStreamBufferDelete(s_fw_stream);
        s_fw_stream = NULL;
    }

    g_ota_status_notify_enabled = false;
}

void ble_srv_ota_register_status_cb(ble_srv_status_cb_t cb)
{
    s_status_cb = cb;
}

void ble_srv_ota_unregister_status_cb(void)
{
    s_status_cb = NULL;
}

void ble_srv_ota_push_status(void)
{
    uint16_t conn_handle = ble_srv_get_conn_handle();

    if (conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGD(TAG, "push_status: skipped, no connection");
        return;
    }

    if (!g_ota_status_notify_enabled) {
        ESP_LOGD(TAG, "push_status: skipped, notify not enabled");
        return;
    }

    uint32_t report = s_fw_bytes_written;

    ble_ota_status_t status = {
        .state          = s_ota_state,
        .error_code     = s_ota_error,
        .fw_size        = s_fw_total_size,
        .bytes_written  = report,
        .progress       = (s_fw_total_size > 0) ? (uint8_t)((report * 100) / s_fw_total_size) : 0,
    };

    ESP_LOGD(TAG, "push_status: state=%d, err=%d, written=%lu/%lu, progress=%d%%",
             status.state, status.error_code,
             (unsigned long)status.bytes_written, (unsigned long)status.fw_size,
             status.progress);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&status, sizeof(status));
    if (!om) {
        ESP_LOGE(TAG, "push_status: failed to allocate mbuf");
        return;
    }

    int rc = ble_gatts_notify_custom(conn_handle, g_ota_status_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "push_status: ble_gatts_notify_custom failed: rc=%d", rc);
    }

    if (s_status_cb) {
        s_status_cb(&status);
    }
}

static void ble_srv_set_ota_state(ble_ota_state_t state, ble_ota_err_t error)
{
    if (xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_ota_state = state;
        s_ota_error = error;
        xSemaphoreGive(s_state_lock);
    }
    ble_srv_ota_push_status();
}

void ble_srv_ota_reset(void)
{
    s_wr_running = false;

    if (s_wr_task) {
        for (int i = 0; i < 20; i++) {
            eTaskState ts = eTaskGetState(s_wr_task);
            if (ts == eDeleted || ts == eInvalid) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        eTaskState ts = eTaskGetState(s_wr_task);
        if (ts != eDeleted && ts != eInvalid) {
            ESP_LOGW(TAG, "Writer task still running, forcing delete");
            vTaskDelete(s_wr_task);
        }
        s_wr_task = NULL;
    }

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    if (s_fw_stream) {
        xStreamBufferReset(s_fw_stream);
    }

    s_fw_total_size = 0;
    s_fw_bytes_written = 0;
    s_total_received = 0;
    s_last_status_update = 0;

    ble_srv_set_ota_state(BLE_OTA_STATE_IDLE, BLE_OTA_ERR_NONE);
}

static bool ble_srv_process_ota_start(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(ble_ota_start_req_t)) {
        ESP_LOGE(TAG, "OTA START payload too short: %d < %d", len, (int)sizeof(ble_ota_start_req_t));
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_CMD);
        return false;
    }

    const ble_ota_start_req_t *req = (const ble_ota_start_req_t *)data;
    s_fw_total_size = req->fw_size;

    ESP_LOGI(TAG, "OTA START: size=%lu bytes, crc=0x%08lX, chunk=%u, ver=v%lu.%lu.%lu",
             (unsigned long)s_fw_total_size, (unsigned long)req->fw_crc, req->chunk_size,
             (unsigned long)(req->fw_version >> 16) & 0xFF,
             (unsigned long)(req->fw_version >> 8) & 0xFF,
             (unsigned long)(req->fw_version) & 0xFF);

    if (s_fw_total_size == 0 || s_fw_total_size > BLE_OTA_MAX_FW_SIZE) {
        ESP_LOGE(TAG, "Invalid fw size: %lu (max: %d)", (unsigned long)s_fw_total_size, BLE_OTA_MAX_FW_SIZE);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    s_target_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_target_partition) {
        ESP_LOGE(TAG, "No update partition available");
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_PARTITION);
        return false;
    }

    ESP_LOGI(TAG, "Target partition: %s @0x%lx (%lu bytes)",
             s_target_partition->label,
             (unsigned long)s_target_partition->address,
             (unsigned long)s_target_partition->size);

    s_wr_running = false;
    if (s_wr_task) {
        for (int i = 0; i < 30; i++) {
            eTaskState ts = eTaskGetState(s_wr_task);
            if (ts == eDeleted || ts == eInvalid) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        eTaskState ts = eTaskGetState(s_wr_task);
        if (ts != eDeleted && ts != eInvalid) {
            ESP_LOGW(TAG, "Old writer task still running, forcing delete");
            vTaskDelete(s_wr_task);
        }
        s_wr_task = NULL;
    }

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    if (s_fw_stream) {
        xStreamBufferReset(s_fw_stream);
    }

    esp_err_t ret = esp_ota_begin(s_target_partition, s_fw_total_size, &s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    s_fw_bytes_written = 0;
    s_total_received = 0;
    s_last_status_update = 0;

    s_wr_running = true;
    BaseType_t ok = xTaskCreate(ble_srv_writer_task, "ota_wr",
                                BLE_SRV_WRITER_TASK_STACK, NULL,
                                BLE_SRV_WRITER_TASK_PRIO, &s_wr_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create writer task");
        s_wr_running = false;
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    ble_srv_set_ota_state(BLE_OTA_STATE_RECEIVING, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "OTA session started successfully");
    return true;
}

static void ble_srv_writer_task(void *arg)
{
    ESP_LOGI(TAG, "OTA writer task started");

    uint8_t write_buf[BLE_SRV_STREAM_TRIGGER];

    while (s_wr_running) {
        size_t recv_len = xStreamBufferReceive(s_fw_stream, write_buf,
                                                sizeof(write_buf),
                                                pdMS_TO_TICKS(50));
        if (recv_len == 0) {
            continue;
        }

        esp_err_t ret = esp_ota_write(s_ota_handle, write_buf, recv_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA write fail @%lu len=%u: %s",
                     (unsigned long)s_fw_bytes_written, (unsigned int)recv_len,
                     esp_err_to_name(ret));
            ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
            s_wr_running = false;
            vTaskDelete(NULL);
            return;
        }

        s_fw_bytes_written += recv_len;

        uint32_t written = s_fw_bytes_written;
        uint8_t pct = (s_fw_total_size > 0) ? (uint8_t)((written * 100) / s_fw_total_size) : 0;
        uint8_t prev_pct = (s_fw_total_size > 0) ? (uint8_t)(((written - recv_len) * 100) / s_fw_total_size) : 0;

        if (pct != prev_pct) {
            ble_srv_ota_push_status();
        }

        if ((pct % 10 == 0 && pct > 0) && pct != prev_pct) {
            ESP_LOGI(TAG, "OTA: %d%% (%lu/%lu)",
                     pct, (unsigned long)written, (unsigned long)s_fw_total_size);
        }
    }

    ESP_LOGI(TAG, "Writer task draining remaining data...");
    size_t drain_len;
    while ((drain_len = xStreamBufferReceive(s_fw_stream, write_buf,
                                              sizeof(write_buf),
                                              pdMS_TO_TICKS(100))) > 0) {
        esp_err_t ret = esp_ota_write(s_ota_handle, write_buf, drain_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA flush fail @%lu: %s",
                     (unsigned long)s_fw_bytes_written, esp_err_to_name(ret));
            ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
            break;
        }
        s_fw_bytes_written += drain_len;
    }
    ble_srv_ota_push_status();

    ESP_LOGI(TAG, "OTA writer task finished, total written=%lu bytes", (unsigned long)s_fw_bytes_written);
    vTaskDelete(NULL);
}

bool ble_srv_ota_process_fw_data(const uint8_t *data, uint16_t len)
{
    if (s_ota_state != BLE_OTA_STATE_RECEIVING) {
        ESP_LOGW(TAG, "FW data ignored, state=%d", s_ota_state);
        return false;
    }

    if (!data || len == 0) {
        ESP_LOGE(TAG, "Invalid params: data=%p, len=%u", data, len);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    if (s_total_received + len > s_fw_total_size) {
        ESP_LOGE(TAG, "FW overflow: %lu + %u > %lu",
                 (unsigned long)s_total_received, len, (unsigned long)s_fw_total_size);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    size_t sent = xStreamBufferSend(s_fw_stream, data, len, 0);
    if (sent != len) {
        ESP_LOGE(TAG, "Stream buffer full! sent=%u, wanted=%u",
                 (unsigned int)sent, (unsigned int)len);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_BUSY);
        return false;
    }

    s_total_received += len;
    return true;
}

static bool ble_srv_process_ota_verify(void)
{
    ESP_LOGI(TAG, "OTA VERIFY command received");

    if (s_ota_state != BLE_OTA_STATE_RECEIVING) {
        ESP_LOGW(TAG, "VERIFY ignored, state=%d", s_ota_state);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_CMD);
        return false;
    }

    s_wr_running = false;
    vTaskDelay(pdMS_TO_TICKS(150));

    if (s_wr_task) {
        eTaskState ts = eTaskGetState(s_wr_task);
        if (ts != eDeleted && ts != eInvalid) {
            ESP_LOGW(TAG, "Writer task still running, forcing delete");
            vTaskDelete(s_wr_task);
        }
        s_wr_task = NULL;
    }

    ESP_LOGI(TAG, "All data flushed, fw_bytes_written=%lu, total_received=%lu",
             (unsigned long)s_fw_bytes_written, (unsigned long)s_total_received);

    if (s_fw_bytes_written != s_fw_total_size) {
        ESP_LOGE(TAG, "FW size mismatch: written %lu != expected %lu",
                 (unsigned long)s_fw_bytes_written, (unsigned long)s_fw_total_size);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    ble_srv_set_ota_state(BLE_OTA_STATE_VERIFYING, BLE_OTA_ERR_NONE);

    esp_err_t ret = esp_ota_end(s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA end fail: %s", esp_err_to_name(ret));
        s_ota_handle = 0;
        ble_srv_set_ota_state(BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_VERIFY_FAILED);
        return false;
    }

    s_ota_handle = 0;
    ble_srv_set_ota_state(BLE_OTA_STATE_VERIFY_OK, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "OTA verify OK");
    return true;
}

static bool ble_srv_process_ota_apply(void)
{
    if (s_ota_state != BLE_OTA_STATE_VERIFY_OK) {
        ESP_LOGW(TAG, "APPLY ignored, state=%d", s_ota_state);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_CMD);
        return false;
    }

    ESP_LOGI(TAG, "APPLY: switch boot to %s @0x%lx",
             s_target_partition->label, (unsigned long)s_target_partition->address);

    ble_srv_set_ota_state(BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);

    esp_err_t ret = esp_ota_set_boot_partition(s_target_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition fail: %s", esp_err_to_name(ret));
        ble_srv_set_ota_state(BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    const esp_partition_t *boot_partition = esp_ota_get_boot_partition();
    ESP_LOGI(TAG, "Boot partition set: %s @0x%lx",
             boot_partition ? boot_partition->label : "N/A",
             boot_partition ? (unsigned long)boot_partition->address : 0);

    ble_srv_set_ota_state(BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "OTA complete! Rebooting...");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return true;
}

void ble_srv_dispatch_ota_cmd(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        ESP_LOGW(TAG, "Empty OTA command write ignored");
        return;
    }

    uint16_t data_len = len - 1;
    if (data_len < 1) {
        ESP_LOGW(TAG, "OTA command too short: no payload");
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_CMD);
        return;
    }

    ble_ota_cmd_t cmd = (ble_ota_cmd_t)data[0];
    ESP_LOGI(TAG, "OTA command received: cmd=0x%02X, payload_len=%u, current_state=%d",
             cmd, data_len, s_ota_state);

    switch (cmd) {
    case BLE_OTA_CMD_START:
        ble_srv_process_ota_start(data + 1, data_len);
        break;
    case BLE_OTA_CMD_ABORT:
          ble_srv_ota_reset();
          ESP_LOGI(TAG, "OTA session aborted by client");
        break;
    case BLE_OTA_CMD_VERIFY:
        ble_srv_process_ota_verify();
        break;
    case BLE_OTA_CMD_APPLY:
        ble_srv_process_ota_apply();
        break;
    default:
        ESP_LOGW(TAG, "Unknown OTA command: 0x%02X", cmd);
        ble_srv_set_ota_state(BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_CMD);
        break;
    }
}

bool ble_srv_ota_get_status(ble_ota_status_t *status)
{
    if (!status) {
        return false;
    }

    uint32_t report = (s_fw_bytes_written > 0) ? s_fw_bytes_written : s_total_received;
    status->state = s_ota_state;
    status->error_code = s_ota_error;
    status->fw_size = s_fw_total_size;
    status->bytes_written = report;
    status->progress = (s_fw_total_size > 0) ? (uint8_t)((report * 100) / s_fw_total_size) : 0;

    return true;
}

ble_ota_state_t ble_srv_ota_get_state(void)
{
    return s_ota_state;
}
