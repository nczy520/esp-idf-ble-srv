#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_gatt.h"
#include "ble_srv.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_heap_caps.h"

static const char *TAG = "OTA_BT";

#define BLE_SRV_WRITE_BUF_SIZE      (64 * 1024)
#define BLE_SRV_NOTIFY_BATCH        CONFIG_BLE_SRV_OTA_BT_NOTIFY_INTERVAL

static SemaphoreHandle_t s_bt_lock = NULL;
static bool s_initialized = false;

static uint8_t s_gen = BLE_OTA_INVALID_GEN;
static esp_ota_handle_t s_ota_handle = 0;
static const esp_partition_t *s_target_partition = NULL;
static uint32_t s_fw_total_size = 0;
static uint32_t s_fw_bytes_written = 0;
static uint32_t s_total_received = 0;

static uint8_t *s_write_buf = NULL;
static uint32_t s_write_buf_len = 0;
static uint8_t s_packet_count = 0;
static bool s_receiving = false;

#define BT_LOCK()   do { if (s_bt_lock) xSemaphoreTakeRecursive(s_bt_lock, portMAX_DELAY); } while(0)
#define BT_UNLOCK() do { if (s_bt_lock) xSemaphoreGiveRecursive(s_bt_lock); } while(0)

static void bt_cleanup_locked(void)
{
    s_receiving = false;
    s_packet_count = 0;

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    s_target_partition = NULL;
    s_write_buf_len = 0;
    s_fw_total_size = 0;
    s_fw_bytes_written = 0;
    s_total_received = 0;
}

static void bt_flush_and_finish(uint8_t gen, ble_ota_state_t result, ble_ota_err_t error)
{
    BT_LOCK();
    bt_cleanup_locked();
    s_gen = BLE_OTA_INVALID_GEN;
    BT_UNLOCK();
    ble_srv_ota_finish(gen, result, error);
}

void ble_srv_ota_bt_handle_abort(void)
{
    BT_LOCK();
    uint8_t gen = s_gen;
    bool is_bt_mode = (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT);
    bool gen_valid = (gen != BLE_OTA_INVALID_GEN) && ble_srv_ota_gen_valid(gen);
    BT_UNLOCK();

    if (!is_bt_mode || !gen_valid) {
        return;
    }
    ESP_LOGW(TAG, "Handling BT OTA abort, gen=%u", gen);
    bt_flush_and_finish(gen, BLE_OTA_STATE_ABORTED, BLE_OTA_ERR_ABORTED);
}

bool ble_srv_ota_bt_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "BT OTA already initialized");
        return true;
    }

    s_bt_lock = xSemaphoreCreateRecursiveMutex();
    if (!s_bt_lock) {
        ESP_LOGE(TAG, "Failed to create BT OTA lock");
        return false;
    }

    s_write_buf = heap_caps_malloc(BLE_SRV_WRITE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_write_buf) {
        ESP_LOGW(TAG, "PSRAM alloc failed for %d bytes, trying internal RAM", BLE_SRV_WRITE_BUF_SIZE);
        s_write_buf = heap_caps_malloc(BLE_SRV_WRITE_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_write_buf) {
        ESP_LOGE(TAG, "Failed to allocate OTA write buffer (%d bytes)", BLE_SRV_WRITE_BUF_SIZE);
        vSemaphoreDelete(s_bt_lock);
        s_bt_lock = NULL;
        return false;
    }

    BT_LOCK();
    s_receiving = false;
    s_gen = BLE_OTA_INVALID_GEN;
    s_ota_handle = 0;
    s_target_partition = NULL;
    s_write_buf_len = 0;
    s_fw_total_size = 0;
    s_fw_bytes_written = 0;
    s_total_received = 0;
    s_packet_count = 0;
    BT_UNLOCK();

    s_initialized = true;
    ESP_LOGI(TAG, "BT OTA initialized, write buffer=%d bytes", BLE_SRV_WRITE_BUF_SIZE);
    return true;
}

void ble_srv_ota_bt_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    if (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT) {
        ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
    }

    BT_LOCK();
    bt_cleanup_locked();
    s_gen = BLE_OTA_INVALID_GEN;
    BT_UNLOCK();

    if (s_write_buf) {
        heap_caps_free(s_write_buf);
        s_write_buf = NULL;
    }

    if (s_bt_lock) {
        vSemaphoreDelete(s_bt_lock);
        s_bt_lock = NULL;
    }

    s_initialized = false;
}

static bool handle_start(const uint8_t *data, uint16_t len)
{
    if (len < sizeof(ble_ota_bt_start_req_t)) {
        ESP_LOGE(TAG, "START payload too short: %u < %u", len, (unsigned)sizeof(ble_ota_bt_start_req_t));
        return false;
    }

    uint8_t gen = ble_srv_ota_begin(BLE_OTA_MODE_BT);
    if (gen == BLE_OTA_INVALID_GEN) {
        ESP_LOGE(TAG, "Cannot begin BT OTA, session busy");
        return false;
    }

    BT_LOCK();
    s_gen = gen;

    const ble_ota_bt_start_req_t *req = (const ble_ota_bt_start_req_t *)data;
    s_fw_total_size = req->fw_size;

    ESP_LOGI(TAG, "BT OTA START: size=%lu, crc=0x%08lX, chunk=%u, ver=v%lu.%lu.%lu, gen=%u",
             (unsigned long)s_fw_total_size, (unsigned long)req->fw_crc, req->chunk_size,
             (unsigned long)(req->fw_version >> 16) & 0xFF,
             (unsigned long)(req->fw_version >> 8) & 0xFF,
             (unsigned long)req->fw_version & 0xFF,
             gen);

    if (s_fw_total_size == 0 || s_fw_total_size > BLE_OTA_MAX_FW_SIZE) {
        ESP_LOGE(TAG, "Invalid fw size: %lu", (unsigned long)s_fw_total_size);
        bt_cleanup_locked();
        s_gen = BLE_OTA_INVALID_GEN;
        BT_UNLOCK();
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    s_target_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_target_partition) {
        ESP_LOGE(TAG, "No update partition");
        bt_cleanup_locked();
        s_gen = BLE_OTA_INVALID_GEN;
        BT_UNLOCK();
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_PARTITION);
        return false;
    }

    ESP_LOGI(TAG, "Target: %s @0x%lx (%lu bytes)",
             s_target_partition->label,
             (unsigned long)s_target_partition->address,
             (unsigned long)s_target_partition->size);

    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }

    s_write_buf_len = 0;
    s_packet_count = 0;
    s_fw_bytes_written = 0;
    s_total_received = 0;

    esp_err_t ret = esp_ota_begin(s_target_partition, s_fw_total_size, &s_ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        bt_cleanup_locked();
        s_gen = BLE_OTA_INVALID_GEN;
        BT_UNLOCK();
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    s_receiving = true;
    uint32_t fw_size = s_fw_total_size;
    BT_UNLOCK();

    ble_srv_ota_set_fw_size(gen, fw_size);
    ble_srv_ota_report_progress(gen, 0, 0);
    ble_srv_ota_set_state(gen, BLE_OTA_STATE_RECEIVING, BLE_OTA_ERR_NONE);

    ESP_LOGI(TAG, "BT OTA session started");
    return true;
}

static bool handle_verify(void)
{
    BT_LOCK();
    uint8_t gen = s_gen;
    bool valid = (gen != BLE_OTA_INVALID_GEN) && ble_srv_ota_gen_valid(gen) &&
                 (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT) && s_receiving;
    if (!valid) {
        BT_UNLOCK();
        ESP_LOGW(TAG, "VERIFY ignored: no BT OTA session");
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_RECEIVING) {
        BT_UNLOCK();
        ESP_LOGW(TAG, "VERIFY ignored, state=%d", ble_srv_ota_get_state());
        return false;
    }

    s_receiving = false;

    if (s_write_buf_len > 0) {
        esp_err_t ret = esp_ota_write(s_ota_handle, s_write_buf, s_write_buf_len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA flush fail: %s", esp_err_to_name(ret));
            bt_cleanup_locked();
            s_gen = BLE_OTA_INVALID_GEN;
            BT_UNLOCK();
            ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
            return false;
        }
        s_fw_bytes_written += s_write_buf_len;
        s_write_buf_len = 0;
    }

    uint32_t bytes_written = s_fw_bytes_written;
    uint32_t total_received = s_total_received;
    uint32_t fw_total = s_fw_total_size;
    esp_ota_handle_t handle = s_ota_handle;

    BT_UNLOCK();

    ble_srv_ota_report_progress(gen, total_received, bytes_written);

    ESP_LOGI(TAG, "Flushed: written=%lu, received=%lu",
             (unsigned long)bytes_written, (unsigned long)total_received);

    if (bytes_written != fw_total) {
        ESP_LOGE(TAG, "Size mismatch: written %lu != expected %lu",
                 (unsigned long)bytes_written, (unsigned long)fw_total);
        bt_flush_and_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFYING, BLE_OTA_ERR_NONE);

    esp_err_t ret = esp_ota_end(handle);
    BT_LOCK();
    s_ota_handle = 0;
    BT_UNLOCK();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        bt_flush_and_finish(gen, BLE_OTA_STATE_VERIFY_FAIL, BLE_OTA_ERR_VERIFY_FAILED);
        return false;
    }

    BT_LOCK();
    s_write_buf_len = 0;
    BT_UNLOCK();

    ble_srv_ota_report_progress(gen, total_received, bytes_written);
    ble_srv_ota_set_state(gen, BLE_OTA_STATE_VERIFY_OK, BLE_OTA_ERR_NONE);
    ESP_LOGI(TAG, "OTA verify OK");
    return true;
}

static bool handle_apply(void)
{
    BT_LOCK();
    uint8_t gen = s_gen;
    bool valid = (gen != BLE_OTA_INVALID_GEN) && ble_srv_ota_gen_valid(gen) &&
                 (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT);
    const esp_partition_t *part = s_target_partition;
    BT_UNLOCK();

    if (!valid) {
        ESP_LOGW(TAG, "APPLY ignored: no BT OTA session");
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_VERIFY_OK) {
        ESP_LOGW(TAG, "APPLY ignored, state=%d", ble_srv_ota_get_state());
        return false;
    }

    if (!part) {
        bt_flush_and_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_NO_PARTITION);
        return false;
    }

    ESP_LOGI(TAG, "APPLY: boot -> %s @0x%lx",
             part->label, (unsigned long)part->address);

    ble_srv_ota_set_state(gen, BLE_OTA_STATE_APPLYING, BLE_OTA_ERR_NONE);

    esp_err_t ret = esp_ota_set_boot_partition(part);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set boot failed: %s", esp_err_to_name(ret));
        bt_flush_and_finish(gen, BLE_OTA_STATE_APPLY_FAIL, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    const esp_partition_t *boot = esp_ota_get_boot_partition();
    ESP_LOGI(TAG, "Boot set: %s @0x%lx",
             boot ? boot->label : "?",
             boot ? (unsigned long)boot->address : 0);

    bt_flush_and_finish(gen, BLE_OTA_STATE_APPLY_OK, BLE_OTA_ERR_NONE);
    return true;
}

bool ble_srv_ota_bt_dispatch_cmd(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        ESP_LOGW(TAG, "Empty command ignored");
        return false;
    }

    ble_ota_bt_cmd_t cmd = (ble_ota_bt_cmd_t)data[0];
    uint16_t payload_len = len - 1;
    const uint8_t *payload = data + 1;

    ESP_LOGI(TAG, "BT OTA cmd=0x%02X payload=%u state=%d",
             cmd, payload_len, ble_srv_ota_get_state());

    switch (cmd) {
    case BLE_OTA_BT_CMD_START:
        return handle_start(payload, payload_len);
    case BLE_OTA_BT_CMD_ABORT:
        if (ble_srv_ota_get_mode() == BLE_OTA_MODE_BT) {
            ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
            return true;
        }
        return false;
    case BLE_OTA_BT_CMD_VERIFY:
        return handle_verify();
    case BLE_OTA_BT_CMD_APPLY:
        return handle_apply();
    default:
        ESP_LOGW(TAG, "Unknown cmd: 0x%02X", cmd);
        return false;
    }
}

bool ble_srv_ota_bt_process_fw_data(const uint8_t *data, uint16_t len)
{
    BT_LOCK();
    uint8_t gen = s_gen;
    if (gen == BLE_OTA_INVALID_GEN || !ble_srv_ota_gen_valid(gen)) {
        BT_UNLOCK();
        return false;
    }

    if (!s_receiving) {
        BT_UNLOCK();
        return false;
    }

    if (ble_srv_ota_get_state() != BLE_OTA_STATE_RECEIVING) {
        BT_UNLOCK();
        return false;
    }

    if (ble_srv_ota_is_abort_requested()) {
        BT_UNLOCK();
        return false;
    }

    if (!data || len == 0) {
        ESP_LOGE(TAG, "Invalid fw_data: data=%p len=%u", data, len);
        bt_cleanup_locked();
        s_gen = BLE_OTA_INVALID_GEN;
        BT_UNLOCK();
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INTERNAL);
        return false;
    }

    if (s_total_received + len > s_fw_total_size) {
        ESP_LOGE(TAG, "FW overflow: %lu+%u > %lu",
                 (unsigned long)s_total_received, len, (unsigned long)s_fw_total_size);
        bt_cleanup_locked();
        s_gen = BLE_OTA_INVALID_GEN;
        BT_UNLOCK();
        ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_INVALID_SIZE);
        return false;
    }

    esp_err_t write_err = ESP_OK;

    if (s_write_buf_len + len > BLE_SRV_WRITE_BUF_SIZE) {
        if (s_write_buf_len > 0) {
            write_err = esp_ota_write(s_ota_handle, s_write_buf, s_write_buf_len);
            if (write_err != ESP_OK) {
                ESP_LOGE(TAG, "OTA write fail: %s", esp_err_to_name(write_err));
                bt_cleanup_locked();
                s_gen = BLE_OTA_INVALID_GEN;
                BT_UNLOCK();
                ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
                return false;
            }
            s_fw_bytes_written += s_write_buf_len;
            s_write_buf_len = 0;
        }
    }

    memcpy(s_write_buf + s_write_buf_len, data, len);
    s_write_buf_len += len;
    s_total_received += len;

    bool flush_now = false;
    if (s_write_buf_len >= BLE_SRV_WRITE_BUF_SIZE) {
        write_err = esp_ota_write(s_ota_handle, s_write_buf, s_write_buf_len);
        if (write_err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write fail: %s", esp_err_to_name(write_err));
            bt_cleanup_locked();
            s_gen = BLE_OTA_INVALID_GEN;
            BT_UNLOCK();
            ble_srv_ota_finish(gen, BLE_OTA_STATE_ERROR, BLE_OTA_ERR_FLASH_WRITE);
            return false;
        }
        s_fw_bytes_written += s_write_buf_len;
        s_write_buf_len = 0;
        flush_now = true;
    }

    s_packet_count++;
    bool do_notify = false;
    uint32_t total_received = s_total_received;
    uint32_t bytes_written = s_fw_bytes_written;
    if (s_packet_count >= BLE_SRV_NOTIFY_BATCH) {
        s_packet_count = 0;
        do_notify = true;
    }
    BT_UNLOCK();

    if (do_notify) {
        ble_srv_ota_report_progress(gen, total_received, bytes_written);
        ble_srv_ota_push_status(gen);
    }

    return true;
}
