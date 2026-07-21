#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_gatt.h"
#include "ble_srv_msg.h"
#include "ble_srv.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_log.h"
#include "esp_system.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "OTA";

static TimerHandle_t s_reset_timer = NULL;

static volatile ble_ota_mode_t s_mode = BLE_OTA_MODE_NONE;
static volatile ble_ota_state_t s_state = BLE_OTA_STATE_IDLE;
static volatile ble_ota_err_t s_error = BLE_OTA_ERR_NONE;
static volatile bool s_abort_requested = false;
static volatile uint8_t s_session_gen = 0;

static uint32_t s_fw_size = 0;
static uint32_t s_bytes_received = 0;
static uint32_t s_bytes_written = 0;

static ble_srv_status_cb_t s_status_cb = NULL;

static void do_push_status(void);
static void reset_timer_cb(TimerHandle_t timer);

static bool is_terminal_state(ble_ota_state_t state)
{
    switch (state) {
    case BLE_OTA_STATE_CHECK_FAIL:
    case BLE_OTA_STATE_VERIFY_FAIL:
    case BLE_OTA_STATE_APPLY_FAIL:
    case BLE_OTA_STATE_ABORTED:
    case BLE_OTA_STATE_ERROR:
        return true;
    default:
        return false;
    }
}

static bool gen_valid(uint8_t gen)
{
    return gen != BLE_OTA_INVALID_GEN && gen == s_session_gen;
}

static void do_push_status(void)
{
    ble_srv_status_cb_t cb = s_status_cb;
    ble_ota_status_t status;
    bool has_notify = false;

    uint16_t conn_handle = ble_srv_get_conn_handle();
    uint16_t ota_status_handle = ble_srv_gatt_get_ota_status_chr_val_handle();
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE && ble_srv_gatt_get_ota_status_notify_enabled()) {
        uint32_t report = (s_bytes_received > s_bytes_written) ? s_bytes_received : s_bytes_written;
        status.state = (uint8_t)s_state;
        status.error_code = (uint8_t)s_error;
        status.fw_size = s_fw_size;
        status.bytes_written = report;
        status.progress = (s_fw_size > 0) ? (uint8_t)((report * 100) / s_fw_size) : 0;
        has_notify = true;

        struct os_mbuf *om = ble_hs_mbuf_from_flat(&status, sizeof(status));
        if (om) {
            int rc = ble_gatts_notify_custom(conn_handle, ota_status_handle, om);
            if (rc != 0) {
                ESP_LOGW(TAG, "notify failed: rc=%d", rc);
            }
        }
    }

    if (cb && has_notify) {
        cb(&status);
    }
}

static void reset_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    ble_srv_msg_send(MSG_OTA_RESET_TIMER, NULL, 0, 0, 0, 0);
}

void ble_srv_ota_reset_to_idle(void)
{
    if (is_terminal_state(s_state)) {
        ESP_LOGI(TAG, "Terminal state timeout, resetting to IDLE");
        s_mode = BLE_OTA_MODE_NONE;
        s_state = BLE_OTA_STATE_IDLE;
        s_error = BLE_OTA_ERR_NONE;
        s_abort_requested = false;
        s_fw_size = 0;
        s_bytes_received = 0;
        s_bytes_written = 0;
        do_push_status();
    }
}

void ble_srv_ota_push_status_internal(void)
{
    do_push_status();
}

bool ble_srv_ota_init(void)
{
    s_reset_timer = xTimerCreate("ota_reset", pdMS_TO_TICKS(BLE_OTA_RESET_DELAY_MS),
                                  pdFALSE, NULL, reset_timer_cb);
    if (!s_reset_timer) {
        ESP_LOGE(TAG, "Failed to create reset timer");
        return false;
    }

    s_mode = BLE_OTA_MODE_NONE;
    s_state = BLE_OTA_STATE_IDLE;
    s_error = BLE_OTA_ERR_NONE;
    s_abort_requested = false;
    s_session_gen = 0;
    s_fw_size = 0;
    s_bytes_received = 0;
    s_bytes_written = 0;
    ESP_LOGI(TAG, "OTA module initialized");
    return true;
}

void ble_srv_ota_deinit(void)
{
    if (s_reset_timer) {
        xTimerStop(s_reset_timer, 0);
    }
    s_abort_requested = true;
    ble_ota_mode_t mode = s_mode;
    s_state = BLE_OTA_STATE_ABORTING;
    s_error = BLE_OTA_ERR_ABORTED;

    if (mode == BLE_OTA_MODE_BT) {
        ble_srv_ota_bt_handle_abort();
    } else if (mode == BLE_OTA_MODE_URL) {
        ble_srv_ota_url_handle_abort();
    }

    vTaskDelay(pdMS_TO_TICKS(BLE_OTA_DEINIT_WAIT_MS));

    if (s_reset_timer) {
        xTimerDelete(s_reset_timer, portMAX_DELAY);
        s_reset_timer = NULL;
    }
}

uint8_t ble_srv_ota_begin(ble_ota_mode_t mode)
{
    if (mode == BLE_OTA_MODE_NONE) {
        ESP_LOGE(TAG, "begin: invalid mode");
        return BLE_OTA_INVALID_GEN;
    }

    if (s_reset_timer) {
        xTimerStop(s_reset_timer, 0);
    }

    if (is_terminal_state(s_state)) {
        s_mode = BLE_OTA_MODE_NONE;
        s_state = BLE_OTA_STATE_IDLE;
        s_error = BLE_OTA_ERR_NONE;
        s_abort_requested = false;
        s_fw_size = 0;
        s_bytes_received = 0;
        s_bytes_written = 0;
    }

    if (s_state != BLE_OTA_STATE_IDLE) {
        ESP_LOGW(TAG, "begin: OTA busy, mode=%d, state=%d", s_mode, s_state);
        return BLE_OTA_INVALID_GEN;
    }

    s_session_gen++;
    if (s_session_gen == BLE_OTA_INVALID_GEN) {
        s_session_gen++;
    }

    uint8_t gen = s_session_gen;
    s_mode = mode;
    s_state = BLE_OTA_STATE_IDLE;
    s_error = BLE_OTA_ERR_NONE;
    s_abort_requested = false;
    s_fw_size = 0;
    s_bytes_received = 0;
    s_bytes_written = 0;

    do_push_status();

    ESP_LOGI(TAG, "OTA session begun: mode=%s, gen=%u",
             mode == BLE_OTA_MODE_BT ? "BT" : (mode == BLE_OTA_MODE_URL ? "URL" : "?"),
             gen);
    return gen;
}

void ble_srv_ota_abort(ble_ota_err_t reason)
{
    if (s_state == BLE_OTA_STATE_IDLE || s_state == BLE_OTA_STATE_APPLY_OK) {
        return;
    }

    if (s_state == BLE_OTA_STATE_ABORTING || s_state == BLE_OTA_STATE_ABORTED || is_terminal_state(s_state)) {
        return;
    }

    s_abort_requested = true;
    ble_ota_state_t prev_state = s_state;
    ble_ota_mode_t mode = s_mode;
    s_state = BLE_OTA_STATE_ABORTING;
    s_error = reason;

    do_push_status();

    ESP_LOGW(TAG, "OTA abort requested, reason=%d, prev_state=%d, mode=%d", reason, prev_state, mode);

    if (mode == BLE_OTA_MODE_BT) {
        ble_srv_ota_bt_handle_abort();
    } else if (mode == BLE_OTA_MODE_URL) {
        ble_srv_ota_url_handle_abort();
    }
}

void ble_srv_ota_finish(uint8_t gen, ble_ota_state_t result, ble_ota_err_t error)
{
    if (!gen_valid(gen)) {
        ESP_LOGW(TAG, "finish: stale gen=%u, current=%u, ignoring", gen, s_session_gen);
        return;
    }

    if (s_mode == BLE_OTA_MODE_NONE && s_state == BLE_OTA_STATE_IDLE) {
        return;
    }

    ble_ota_mode_t finished_mode = s_mode;
    bool is_apply_ok = (result == BLE_OTA_STATE_APPLY_OK);
    bool is_terminal = is_terminal_state(result);

    s_state = result;
    s_error = error;
    s_abort_requested = false;

    if (is_terminal) {
        s_mode = BLE_OTA_MODE_NONE;
        s_abort_requested = false;
        if (s_reset_timer) {
            xTimerReset(s_reset_timer, 0);
            xTimerStart(s_reset_timer, 0);
        }
    }

    do_push_status();

    ESP_LOGI(TAG, "OTA session finished: mode=%s, result=%d, error=%d, gen=%u",
             finished_mode == BLE_OTA_MODE_BT ? "BT" : (finished_mode == BLE_OTA_MODE_URL ? "URL" : "?"),
             result, error, gen);

    if (is_apply_ok) {
        ESP_LOGI(TAG, "OTA apply OK, rebooting in 3s...");
        ble_srv_schedule_restart(BLE_OTA_RESTART_DELAY_MS);
    }
}

bool ble_srv_ota_is_abort_requested(void)
{
    return s_abort_requested;
}

bool ble_srv_ota_is_active(void)
{
    return (s_state != BLE_OTA_STATE_IDLE);
}

ble_ota_mode_t ble_srv_ota_get_mode(void)
{
    return s_mode;
}

ble_ota_state_t ble_srv_ota_get_state(void)
{
    return s_state;
}

uint8_t ble_srv_ota_get_current_gen(void)
{
    return s_session_gen;
}

bool ble_srv_ota_gen_valid(uint8_t gen)
{
    return gen_valid(gen);
}

void ble_srv_ota_set_state(uint8_t gen, ble_ota_state_t state, ble_ota_err_t error)
{
    if (!gen_valid(gen)) {
        return;
    }
    if (s_state != state || s_error != error) {
        s_state = state;
        s_error = error;
        do_push_status();
    }
}

void ble_srv_ota_set_fw_size(uint8_t gen, uint32_t fw_size)
{
    if (!gen_valid(gen)) {
        return;
    }
    s_fw_size = fw_size;
    s_bytes_received = 0;
    s_bytes_written = 0;
}

void ble_srv_ota_report_progress(uint8_t gen, uint32_t bytes_received, uint32_t bytes_written)
{
    if (!gen_valid(gen)) {
        return;
    }
    s_bytes_received = bytes_received;
    s_bytes_written = bytes_written;
}

void ble_srv_ota_push_status(uint8_t gen)
{
    if (!gen_valid(gen)) {
        return;
    }
    do_push_status();
}

bool ble_srv_ota_get_status(ble_ota_status_t *status)
{
    if (!status) {
        return false;
    }
    uint32_t report = (s_bytes_received > s_bytes_written) ? s_bytes_received : s_bytes_written;
    status->state = (uint8_t)s_state;
    status->error_code = (uint8_t)s_error;
    status->fw_size = s_fw_size;
    status->bytes_written = report;
    status->progress = (s_fw_size > 0) ? (uint8_t)((report * 100) / s_fw_size) : 0;
    return true;
}

void ble_srv_ota_register_status_cb(ble_srv_status_cb_t cb)
{
    s_status_cb = cb;
}
