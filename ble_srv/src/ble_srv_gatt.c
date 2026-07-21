#include "ble_srv_gatt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_device.h"
#include "ble_srv_wifi.h"
#include "ble_srv_led.h"
#include "ble_srv_log.h"
#include "ble_srv_msg.h"
#include "ble_srv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "BLE_SRV_GATT";

#define GATT_RESTART_CMD_DELAY_MS    100
#define GATT_RESTART_CHR_DELAY_MS    500

#define BLE_SRV_GATT_WRITE_BUF_SIZE 512

static int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);

static uint8_t *s_write_buf = NULL;

static uint8_t s_partition_index = 0;

static uint16_t s_srv_cmd_chr_val_handle = 0;
static uint16_t s_srv_info_chr_val_handle = 0;
static uint16_t s_srv_memory_chr_val_handle = 0;
static uint16_t s_srv_cpu_chr_val_handle = 0;
static uint16_t s_srv_flash_chr_val_handle = 0;
static uint16_t s_srv_partition_chr_val_handle = 0;
static uint16_t s_srv_restart_chr_val_handle = 0;

static uint16_t s_ota_bt_cmd_chr_val_handle = 0;
static uint16_t s_ota_bt_fw_data_chr_val_handle = 0;
static uint16_t s_ota_status_chr_val_handle = 0;
static bool s_ota_status_notify_enabled = false;

#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
static uint16_t s_ota_url_chr_val_handle = 0;
#endif

static uint16_t s_wifi_config_chr_val_handle = 0;
static uint16_t s_wifi_status_chr_val_handle = 0;
static uint16_t s_wifi_ctrl_chr_val_handle = 0;
static bool s_wifi_status_notify_enabled = false;

#ifdef CONFIG_BLE_SRV_LED_ENABLED
static uint16_t s_led_ctrl_chr_val_handle = 0;
static uint16_t s_led_color_chr_val_handle = 0;
static uint16_t s_led_effect_chr_val_handle = 0;
#endif

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
static uint16_t s_srv_auth_chr_val_handle = 0;
static char s_auth_pin[BLE_SRV_AUTH_PIN_MAX_LEN + 1] = {0};
static volatile bool s_conn_authenticated = false;
#endif

static uint16_t s_srv_log_chr_val_handle = 0;
static bool s_log_notify_enabled = false;

static uint16_t s_srv_custom_cmd_chr_val_handle = 0;
static bool s_custom_cmd_notify_enabled = false;
static ble_srv_custom_cmd_cb_t s_custom_cmd_cb = NULL;

static uint16_t s_srv_log_http_ctrl_chr_val_handle = 0;
static uint16_t s_srv_log_storage_chr_val_handle = 0;

static const ble_uuid16_t s_srv_svc_uuid = BLE_UUID16_INIT(BLE_SRV_SVC_UUID);
static const ble_uuid16_t s_srv_cmd_chr_uuid = BLE_UUID16_INIT(BLE_SRV_CMD_CHAR_UUID);
static const ble_uuid16_t s_srv_info_chr_uuid = BLE_UUID16_INIT(BLE_SRV_INFO_CHAR_UUID);
static const ble_uuid16_t s_srv_memory_chr_uuid = BLE_UUID16_INIT(BLE_SRV_MEMORY_CHAR_UUID);
static const ble_uuid16_t s_srv_cpu_chr_uuid = BLE_UUID16_INIT(BLE_SRV_CPU_CHAR_UUID);
static const ble_uuid16_t s_srv_flash_chr_uuid = BLE_UUID16_INIT(BLE_SRV_FLASH_CHAR_UUID);
static const ble_uuid16_t s_srv_partition_chr_uuid = BLE_UUID16_INIT(BLE_SRV_PARTITION_CHAR_UUID);
static const ble_uuid16_t s_srv_restart_chr_uuid = BLE_UUID16_INIT(BLE_SRV_RESTART_CHAR_UUID);

static const ble_uuid16_t s_ota_svc_uuid = BLE_UUID16_INIT(BLE_OTA_SVC_UUID);
static const ble_uuid16_t s_ota_bt_cmd_chr_uuid = BLE_UUID16_INIT(0xFFD1);
static const ble_uuid16_t s_ota_bt_fw_data_chr_uuid = BLE_UUID16_INIT(0xFFD2);
static const ble_uuid16_t s_ota_status_chr_uuid = BLE_UUID16_INIT(BLE_OTA_STATUS_CHAR_UUID);

#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
static const ble_uuid16_t s_ota_url_chr_uuid = BLE_UUID16_INIT(0xFFD4);
#endif

static const ble_uuid16_t s_wifi_svc_uuid = BLE_UUID16_INIT(0xFFC0);
static const ble_uuid16_t s_wifi_config_chr_uuid = BLE_UUID16_INIT(0xFFC1);
static const ble_uuid16_t s_wifi_status_chr_uuid = BLE_UUID16_INIT(0xFFC2);
static const ble_uuid16_t s_wifi_ctrl_chr_uuid = BLE_UUID16_INIT(0xFFC3);

#ifdef CONFIG_BLE_SRV_LED_ENABLED
static const ble_uuid16_t s_led_svc_uuid = BLE_UUID16_INIT(0xFFB0);
static const ble_uuid16_t s_led_ctrl_chr_uuid = BLE_UUID16_INIT(0xFFB1);
static const ble_uuid16_t s_led_color_chr_uuid = BLE_UUID16_INIT(0xFFB2);
static const ble_uuid16_t s_led_effect_chr_uuid = BLE_UUID16_INIT(0xFFB3);
#endif

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
static const ble_uuid16_t s_auth_chr_uuid = BLE_UUID16_INIT(BLE_SRV_AUTH_CHAR_UUID);
#endif

static const ble_uuid16_t s_log_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_CHAR_UUID);
static const ble_uuid16_t s_log_http_ctrl_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_HTTP_CTRL_CHAR_UUID);
static const ble_uuid16_t s_log_storage_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_STORAGE_CHAR_UUID);

static const ble_uuid16_t s_custom_cmd_chr_uuid = BLE_UUID16_INIT(BLE_SRV_CUSTOM_CMD_CHAR_UUID);

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_srv_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_srv_cmd_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_srv_cmd_chr_val_handle,
            },
            {
                .uuid = &s_srv_info_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_srv_info_chr_val_handle,
            },
            {
                .uuid = &s_srv_memory_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_srv_memory_chr_val_handle,
            },
            {
                .uuid = &s_srv_cpu_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_srv_cpu_chr_val_handle,
            },
            {
                .uuid = &s_srv_flash_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_srv_flash_chr_val_handle,
            },
            {
                .uuid = &s_srv_partition_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_srv_partition_chr_val_handle,
            },
            {
                .uuid = &s_srv_restart_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_restart_chr_val_handle,
            },
#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
            {
                .uuid = &s_auth_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_auth_chr_val_handle,
            },
#endif
            {
                .uuid = &s_log_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_log_chr_val_handle,
            },
            {
                .uuid = &s_custom_cmd_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_custom_cmd_chr_val_handle,
            },
            {
                .uuid = &s_log_http_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_srv_log_http_ctrl_chr_val_handle,
            },
            {
                .uuid = &s_log_storage_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_srv_log_storage_chr_val_handle,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ota_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_ota_bt_cmd_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_ota_bt_cmd_chr_val_handle,
            },
            {
                .uuid = &s_ota_bt_fw_data_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_ota_bt_fw_data_chr_val_handle,
            },
            {
                .uuid = &s_ota_status_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_ota_status_chr_val_handle,
            },
#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
            {
                .uuid = &s_ota_url_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_ota_url_chr_val_handle,
            },
#endif
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_wifi_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_wifi_config_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_wifi_config_chr_val_handle,
            },
            {
                .uuid = &s_wifi_status_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_wifi_status_chr_val_handle,
            },
            {
                .uuid = &s_wifi_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_wifi_ctrl_chr_val_handle,
            },
            { 0 },
        },
    },
#ifdef CONFIG_BLE_SRV_LED_ENABLED
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_led_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_led_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_led_ctrl_chr_val_handle,
            },
            {
                .uuid = &s_led_color_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_led_color_chr_val_handle,
            },
            {
                .uuid = &s_led_effect_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &s_led_effect_chr_val_handle,
            },
            { 0 },
        },
    },
#endif
    { 0 },
};

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
static void ble_srv_auth_fail_disconnect(uint16_t conn_handle)
{
    uint8_t auth_fail = 0x00;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&auth_fail, sizeof(auth_fail));
    if (om) {
        ble_gatts_notify_custom(conn_handle, s_srv_auth_chr_val_handle, om);
    }
    ESP_LOGW(TAG, "Unauthenticated access, disconnecting (conn=%d)", conn_handle);
    BLE_SRV_LOGW(TAG, "Unauthenticated access, disconnecting (conn=%d)", conn_handle);
    ble_gap_terminate(conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}
#endif

static int handle_read_chr(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt)
{
    int rc;

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
    if (attr_handle == s_srv_auth_chr_val_handle) {
        uint8_t authed = s_conn_authenticated ? 1 : 0;
        rc = os_mbuf_append(ctxt->om, &authed, sizeof(authed));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (!s_conn_authenticated) {
        ESP_LOGW(TAG, "Read denied: not authenticated (handle=%d)", attr_handle);
        ble_srv_auth_fail_disconnect(conn_handle);
        return BLE_SRV_AUTH_ERR_NOT_AUTH;
    }
#endif

    if (attr_handle == s_srv_info_chr_val_handle) {
        ble_srv_device_info_t info;
        if (ble_srv_get_device_info(&info)) {
            rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_srv_memory_chr_val_handle) {
        ble_srv_memory_info_t info;
        if (ble_srv_get_memory_info(&info)) {
            rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_srv_cpu_chr_val_handle) {
        ble_srv_cpu_info_t info;
        if (ble_srv_get_cpu_info(&info)) {
            rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_srv_flash_chr_val_handle) {
        ble_srv_flash_info_t info;
        if (ble_srv_get_flash_info(&info)) {
            rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_srv_partition_chr_val_handle) {
        ble_srv_partition_info_t info;
        if (ble_srv_get_partition_info(s_partition_index, &info)) {
            rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_ota_status_chr_val_handle) {
        ble_ota_status_t status;
        if (ble_srv_ota_get_status(&status)) {
            rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_srv_log_http_ctrl_chr_val_handle) {
        uint8_t running = ble_srv_log_http_is_running() ? 1 : 0;
        rc = os_mbuf_append(ctxt->om, &running, sizeof(running));
        if (rc != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
        if (running) {
            ble_wifi_status_t wifi_status;
            if (ble_srv_wifi_get_status(&wifi_status) && wifi_status.connected) {
                uint8_t ip_bytes[4];
                ip_bytes[0] = (wifi_status.ip_address >> 0) & 0xFF;
                ip_bytes[1] = (wifi_status.ip_address >> 8) & 0xFF;
                ip_bytes[2] = (wifi_status.ip_address >> 16) & 0xFF;
                ip_bytes[3] = (wifi_status.ip_address >> 24) & 0xFF;
                char url[64];
                int url_len = snprintf(url, sizeof(url), "http://%u.%u.%u.%u:%d/",
                                       (unsigned)ip_bytes[0], (unsigned)ip_bytes[1],
                                       (unsigned)ip_bytes[2], (unsigned)ip_bytes[3],
                                       BLE_SRV_LOG_HTTP_PORT);
                uint8_t url_len_byte = (uint8_t)url_len;
                rc = os_mbuf_append(ctxt->om, &url_len_byte, sizeof(url_len_byte));
                if (rc != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
                rc = os_mbuf_append(ctxt->om, url, url_len);
                if (rc != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
            } else {
                uint8_t zero = 0;
                rc = os_mbuf_append(ctxt->om, &zero, sizeof(zero));
                if (rc != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
        }
#else
        uint8_t zero = 0;
        rc = os_mbuf_append(ctxt->om, &zero, sizeof(zero));
        if (rc != 0) return BLE_ATT_ERR_INSUFFICIENT_RES;
#endif
        return 0;
    } else if (attr_handle == s_srv_log_storage_chr_val_handle) {
        ble_srv_log_storage_info_t info;
        if (ble_srv_log_get_storage_info(&info)) {
            rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    else if (attr_handle == s_wifi_status_chr_val_handle) {
        ble_wifi_status_t status;
        if (ble_srv_wifi_get_status(&status)) {
            rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
#endif
#ifdef CONFIG_BLE_SRV_LED_ENABLED
    else if (attr_handle == s_led_ctrl_chr_val_handle) {
        ble_led_status_t status;
        if (ble_srv_led_get_status(&status)) {
            rc = os_mbuf_append(ctxt->om, &status.on, sizeof(status.on));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_led_color_chr_val_handle) {
        ble_led_status_t status;
        if (ble_srv_led_get_status(&status)) {
            uint8_t rgb[3] = {status.red, status.green, status.blue};
            rc = os_mbuf_append(ctxt->om, rgb, sizeof(rgb));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    } else if (attr_handle == s_led_effect_chr_val_handle) {
        ble_led_status_t status;
        if (ble_srv_led_get_status(&status)) {
            ble_led_effect_config_t cfg = {
                .effect = status.effect,
                .speed = status.speed,
            };
            rc = os_mbuf_append(ctxt->om, &cfg, sizeof(cfg));
            return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }
#endif
    return BLE_ATT_ERR_UNLIKELY;
}

void ble_srv_gatt_handle_write(uint16_t conn_handle, uint16_t attr_handle,
                               const uint8_t *data, uint16_t data_len)
{
#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
    if (attr_handle == s_srv_auth_chr_val_handle) {
        if (data_len == 0 || data_len > BLE_SRV_AUTH_PIN_MAX_LEN) {
            return;
        }
        bool match = (strlen(s_auth_pin) == data_len &&
                      memcmp(data, s_auth_pin, data_len) == 0);
        s_conn_authenticated = match;
        if (match) {
            ESP_LOGI(TAG, "PIN authentication success (conn=%d)", conn_handle);
            BLE_SRV_LOGI(TAG, "PIN authentication success (conn=%d)", conn_handle);
        } else {
            ESP_LOGW(TAG, "PIN authentication failed (conn=%d)", conn_handle);
            BLE_SRV_LOGW(TAG, "PIN authentication failed (conn=%d)", conn_handle);
        }
        return;
    }
    if (!s_conn_authenticated) {
        ESP_LOGW(TAG, "Write denied: not authenticated (handle=%d)", attr_handle);
        ble_srv_auth_fail_disconnect(conn_handle);
        return;
    }
#endif

    if (attr_handle == s_ota_bt_cmd_chr_val_handle) {
        if (data_len < 1) return;
        ble_srv_ota_bt_dispatch_cmd(data, data_len);
    }
#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
    else if (attr_handle == s_ota_url_chr_val_handle) {
        if (data_len < 1) return;
        ble_ota_url_cmd_t cmd = (ble_ota_url_cmd_t)data[0];
        switch (cmd) {
        case BLE_OTA_URL_CMD_START_URL:
            if (data_len > 1) {
                uint16_t url_len = data_len - 1;
                if (url_len > BLE_OTA_URL_MAX_URL_LEN) url_len = BLE_OTA_URL_MAX_URL_LEN;
                char url[BLE_OTA_URL_MAX_URL_LEN + 1] = {0};
                memcpy(url, data + 1, url_len);
                url[url_len] = '\0';
                ble_srv_ota_url_start(url);
            }
            break;
        case BLE_OTA_URL_CMD_START_DEFAULT: {
            const char *default_url = CONFIG_BLE_SRV_OTA_URL_DEFAULT;
            if (strlen(default_url) == 0) {
                ESP_LOGE(TAG, "Default OTA URL is empty");
                return;
            }
            ble_srv_ota_url_start(default_url);
            break;
        }
        case BLE_OTA_URL_CMD_ABORT:
            ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
            break;
        default:
            break;
        }
    }
#endif
    else if (attr_handle == s_srv_cmd_chr_val_handle) {
        ble_srv_cmd_t cmd = (ble_srv_cmd_t)data[0];
        ESP_LOGI(TAG, "SRV command: 0x%02X", cmd);
        BLE_SRV_LOGI(TAG, "SRV command: 0x%02X", cmd);
        switch (cmd) {
        case BLE_SRV_CMD_RESTART:
            ble_srv_schedule_restart(GATT_RESTART_CMD_DELAY_MS);
            break;
        default:
            break;
        }
    } else if (attr_handle == s_ota_bt_fw_data_chr_val_handle) {
        ble_srv_ota_bt_process_fw_data(data, data_len);
    } else if (attr_handle == s_srv_partition_chr_val_handle) {
        if (data_len > 0) {
            s_partition_index = data[0];
        }
    } else if (attr_handle == s_srv_restart_chr_val_handle) {
        uint8_t restart_response = 0x01;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&restart_response, sizeof(restart_response));
        if (om) {
            ble_gatts_notify_custom(conn_handle, s_srv_restart_chr_val_handle, om);
        }
        ble_srv_schedule_restart(GATT_RESTART_CHR_DELAY_MS);
    }
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    else if (attr_handle == s_wifi_config_chr_val_handle) {
        if (data_len >= 2) {
            uint8_t ssid_len = data[0];
            if (ssid_len > 32) ssid_len = 32;
            if (data_len < (uint16_t)(1 + ssid_len + 1)) return;
            uint8_t pass_len = data[1 + ssid_len];
            if (pass_len > 64) pass_len = 64;
            if (data_len < (uint16_t)(1 + ssid_len + 1 + pass_len)) return;
            char ssid[33] = {0};
            char password[65] = {0};
            memcpy(ssid, data + 1, ssid_len);
            memcpy(password, data + 1 + ssid_len + 1, pass_len);
            BLE_SRV_LOGI(TAG, "WiFi config write, SSID: %s", ssid);
            ble_srv_wifi_connect(ssid, password);
        }
    } else if (attr_handle == s_wifi_ctrl_chr_val_handle) {
        ble_wifi_ctrl_cmd_t cmd = (ble_wifi_ctrl_cmd_t)data[0];
        switch (cmd) {
        case BLE_WIFI_CTRL_FORGET:
            ble_srv_wifi_forget();
            break;
        case BLE_WIFI_CTRL_NTP_SYNC:
#ifdef CONFIG_BLE_SRV_NTP_ENABLED
            ble_srv_ntp_sync();
#else
            ESP_LOGW(TAG, "NTP sync not enabled");
#endif
            break;
        default:
            break;
        }
    }
#endif
#ifdef CONFIG_BLE_SRV_LED_ENABLED
    else if (attr_handle == s_led_ctrl_chr_val_handle) {
        ble_led_ctrl_t ctrl = (ble_led_ctrl_t)data[0];
        BLE_SRV_LOGI(TAG, "LED control command: %d", (int)ctrl);
        if (ctrl == BLE_LED_CTRL_ON) {
            ble_srv_led_set_on(true);
        } else if (ctrl == BLE_LED_CTRL_OFF) {
            ble_srv_led_set_on(false);
        }
    } else if (attr_handle == s_led_color_chr_val_handle) {
        if (data_len >= 3) {
            ble_srv_led_set_rgb(data[0], data[1], data[2]);
        }
    } else if (attr_handle == s_led_effect_chr_val_handle) {
        if (data_len >= sizeof(ble_led_effect_config_t)) {
            const ble_led_effect_config_t *cfg = (const ble_led_effect_config_t *)data;
            ble_srv_led_set_effect((ble_led_effect_t)cfg->effect, cfg->speed);
        } else if (data_len >= 1) {
            ble_srv_led_set_effect((ble_led_effect_t)data[0], 50);
        }
    }
#endif
    else if (attr_handle == s_srv_custom_cmd_chr_val_handle) {
        BLE_SRV_LOGI(TAG, "Custom command received (len=%d)", data_len);
        if (s_custom_cmd_cb) {
            uint8_t resp_buf[512];
            uint16_t resp_len = 0;
            int rc = s_custom_cmd_cb(conn_handle, data, data_len,
                                     resp_buf, sizeof(resp_buf), &resp_len);
            if (rc == 0 && resp_len > 0 && s_custom_cmd_notify_enabled) {
                struct os_mbuf *om = ble_hs_mbuf_from_flat(resp_buf, resp_len);
                if (om) {
                    ble_gatts_notify_custom(conn_handle, s_srv_custom_cmd_chr_val_handle, om);
                }
            }
        }
    } else if (attr_handle == s_srv_log_http_ctrl_chr_val_handle) {
        if (data_len > 0) {
            uint8_t cmd = data[0];
            if (cmd == BLE_SRV_LOG_HTTP_CMD_START) {
                ESP_LOGI(TAG, "HTTP server start requested");
                BLE_SRV_LOGI(TAG, "HTTP server start requested");
                ble_srv_log_http_start();
            } else if (cmd == BLE_SRV_LOG_HTTP_CMD_STOP) {
                ESP_LOGI(TAG, "HTTP server stop requested");
                BLE_SRV_LOGI(TAG, "HTTP server stop requested");
                ble_srv_log_http_stop();
            } else if (cmd == BLE_SRV_LOG_HTTP_CMD_WRITE_LOG && data_len > 1) {
                char msg[256];
                uint16_t msg_len = data_len - 1;
                if (msg_len > sizeof(msg) - 1) msg_len = sizeof(msg) - 1;
                memcpy(msg, data + 1, msg_len);
                msg[msg_len] = '\0';
                BLE_SRV_LOGI("CLIENT", "%s", msg);
            } else if (cmd == BLE_SRV_LOG_HTTP_CMD_FORMAT_LITTLEFS) {
                ESP_LOGI(TAG, "Format LittleFS requested");
                BLE_SRV_LOGI(TAG, "Format LittleFS requested");
                ble_srv_log_format_littlefs();
            } else if (cmd == BLE_SRV_LOG_HTTP_CMD_SET_LEVEL && data_len > 1) {
                uint8_t level = data[1];
                if (level <= BLE_SRV_LOG_LEVEL_VERBOSE) {
                    ESP_LOGI(TAG, "Set log level: %d", level);
                    ble_srv_log_set_level((ble_srv_log_level_t)level);
                }
            }
        }
    }
}

void ble_srv_gatt_handle_read(uint16_t conn_handle, uint16_t attr_handle)
{
    (void)conn_handle;
    (void)attr_handle;
}

int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        return handle_read_chr(conn_handle, attr_handle, ctxt);

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
        if (!s_conn_authenticated && attr_handle != s_srv_auth_chr_val_handle) {
            ESP_LOGW(TAG, "Write denied: not authenticated (handle=%d)", attr_handle);
            ble_srv_auth_fail_disconnect(conn_handle);
            return BLE_SRV_AUTH_ERR_NOT_AUTH;
        }
#endif

        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > BLE_SRV_GATT_WRITE_BUF_SIZE) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint16_t data_len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, s_write_buf, om_len, &data_len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        ble_srv_gatt_handle_write(conn_handle, attr_handle, s_write_buf, data_len);
        return 0;
    }

    default:
        return BLE_ATT_ERR_UNLIKELY;
    }
}

const struct ble_gatt_svc_def *ble_srv_get_gatt_svcs(void)
{
    return s_gatt_svcs;
}

void ble_srv_gatt_deinit(void)
{
    if (s_write_buf) {
        heap_caps_free(s_write_buf);
        s_write_buf = NULL;
    }
}

bool ble_srv_gatt_init(void)
{
    s_write_buf = heap_caps_malloc(BLE_SRV_GATT_WRITE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_write_buf) s_write_buf = heap_caps_malloc(BLE_SRV_GATT_WRITE_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_write_buf) return false;

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
    const char *pin = CONFIG_BLE_SRV_AUTH_PIN;
    size_t pin_len = strlen(pin);
    if (pin_len > BLE_SRV_AUTH_PIN_MAX_LEN) pin_len = BLE_SRV_AUTH_PIN_MAX_LEN;
    memcpy(s_auth_pin, pin, pin_len);
    s_auth_pin[pin_len] = '\0';
    s_conn_authenticated = false;
    ESP_LOGI(TAG, "PIN auth enabled, PIN length=%zu", pin_len);
#endif
    return true;
}

bool ble_srv_gatt_get_ota_status_notify_enabled(void)
{
    return s_ota_status_notify_enabled;
}

void ble_srv_gatt_set_ota_status_notify_enabled(bool enabled)
{
    s_ota_status_notify_enabled = enabled;
}

uint16_t ble_srv_gatt_get_ota_status_chr_val_handle(void)
{
    return s_ota_status_chr_val_handle;
}

bool ble_srv_gatt_get_wifi_status_notify_enabled(void)
{
    return s_wifi_status_notify_enabled;
}

void ble_srv_gatt_set_wifi_status_notify_enabled(bool enabled)
{
    s_wifi_status_notify_enabled = enabled;
}

uint16_t ble_srv_gatt_get_wifi_status_chr_val_handle(void)
{
    return s_wifi_status_chr_val_handle;
}

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
bool ble_srv_gatt_is_auth_enabled(void)
{
    return true;
}

bool ble_srv_gatt_is_conn_authenticated(uint16_t conn_handle)
{
    if (conn_handle != ble_srv_get_conn_handle()) {
        return false;
    }
    return s_conn_authenticated;
}

void ble_srv_gatt_set_conn_authenticated(uint16_t conn_handle, bool authed)
{
    if (conn_handle != ble_srv_get_conn_handle()) {
        return;
    }
    s_conn_authenticated = authed;
}

void ble_srv_gatt_clear_auth_state(uint16_t conn_handle)
{
    (void)conn_handle;
    s_conn_authenticated = false;
    ESP_LOGI(TAG, "Auth state cleared (conn=%d)", conn_handle);
}

uint16_t ble_srv_gatt_get_auth_chr_val_handle(void)
{
    return s_srv_auth_chr_val_handle;
}
#else
bool ble_srv_gatt_is_auth_enabled(void)
{
    return false;
}

bool ble_srv_gatt_is_conn_authenticated(uint16_t conn_handle)
{
    (void)conn_handle;
    return true;
}

void ble_srv_gatt_set_conn_authenticated(uint16_t conn_handle, bool authed)
{
    (void)conn_handle;
    (void)authed;
}

void ble_srv_gatt_clear_auth_state(uint16_t conn_handle)
{
    (void)conn_handle;
}

uint16_t ble_srv_gatt_get_auth_chr_val_handle(void)
{
    return 0;
}
#endif

bool ble_srv_gatt_log_notify_enabled(void)
{
    return s_log_notify_enabled;
}

void ble_srv_gatt_set_log_notify_enabled(bool enabled)
{
    s_log_notify_enabled = enabled;
}

uint16_t ble_srv_gatt_get_log_chr_val_handle(void)
{
    return s_srv_log_chr_val_handle;
}

static uint16_t s_log_conn_handle = 0;

void ble_srv_gatt_set_log_conn_handle(uint16_t conn_handle)
{
    s_log_conn_handle = conn_handle;
}

static uint16_t s_custom_cmd_conn_handle = 0;

void ble_srv_gatt_set_custom_cmd_conn_handle(uint16_t conn_handle)
{
    s_custom_cmd_conn_handle = conn_handle;
}

static char ble_srv_gatt_log_level_char(ble_srv_log_level_t level)
{
    switch (level) {
    case BLE_SRV_LOG_LEVEL_ERROR:   return 'E';
    case BLE_SRV_LOG_LEVEL_WARN:    return 'W';
    case BLE_SRV_LOG_LEVEL_INFO:    return 'I';
    case BLE_SRV_LOG_LEVEL_DEBUG:   return 'D';
    case BLE_SRV_LOG_LEVEL_VERBOSE: return 'V';
    default:                        return 'I';
    }
}

void ble_srv_gatt_log_send_raw(ble_srv_log_level_t level, const char *msg)
{
    if (!s_log_notify_enabled || s_srv_log_chr_val_handle == 0) {
        return;
    }
    if (!msg) {
        return;
    }
    uint16_t conn = s_log_conn_handle;
    if (conn == 0 || conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    char line[BLE_SRV_LOG_MAX_LEN + 8];
    int prefix_len = snprintf(line, sizeof(line), "[%c] ", ble_srv_gatt_log_level_char(level));
    if (prefix_len < 0 || prefix_len >= (int)sizeof(line)) {
        prefix_len = 0;
    }
    size_t msg_len = strnlen(msg, BLE_SRV_LOG_MAX_LEN);
    if (prefix_len + msg_len >= sizeof(line)) {
        msg_len = sizeof(line) - prefix_len - 1;
    }
    memcpy(line + prefix_len, msg, msg_len);
    line[prefix_len + msg_len] = '\0';
    size_t total_len = prefix_len + msg_len;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(line, total_len);
    if (om) {
        ble_gatts_notify_custom(conn, s_srv_log_chr_val_handle, om);
    }
}

void ble_srv_gatt_log_send(ble_srv_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (!s_log_notify_enabled || s_srv_log_chr_val_handle == 0) {
        return;
    }
    if (!fmt) {
        return;
    }

    char body[BLE_SRV_LOG_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }

    char line[BLE_SRV_LOG_MAX_LEN + 32];
    int written;
    if (tag && tag[0]) {
        written = snprintf(line, sizeof(line), "[%c] [%s] %s", (char)level, tag, body);
    } else {
        written = snprintf(line, sizeof(line), "[%c] %s", (char)level, body);
    }
    if (written < 0) {
        return;
    }
    if (written >= (int)sizeof(line)) {
        written = (int)sizeof(line) - 1;
        line[written] = '\0';
    }

    uint16_t conn = s_log_conn_handle;
    if (conn == 0 || conn == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(line, written);
    if (om) {
        ble_gatts_notify_custom(conn, s_srv_log_chr_val_handle, om);
    }
}

void ble_srv_gatt_custom_cmd_notify(uint16_t conn_handle, const uint8_t *data, uint16_t data_len)
{
    if (!s_custom_cmd_notify_enabled || s_srv_custom_cmd_chr_val_handle == 0) {
        return;
    }
    if (!data || data_len == 0) {
        return;
    }
    if (conn_handle == 0 || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, data_len);
    if (om) {
        ble_gatts_notify_custom(conn_handle, s_srv_custom_cmd_chr_val_handle, om);
    }
}

void ble_srv_gatt_set_custom_cmd_callback(ble_srv_custom_cmd_cb_t cb)
{
    s_custom_cmd_cb = cb;
}

bool ble_srv_gatt_get_custom_cmd_notify_enabled(void)
{
    return s_custom_cmd_notify_enabled;
}

void ble_srv_gatt_set_custom_cmd_notify_enabled(bool enabled)
{
    s_custom_cmd_notify_enabled = enabled;
}

uint16_t ble_srv_gatt_get_custom_cmd_chr_val_handle(void)
{
    return s_srv_custom_cmd_chr_val_handle;
}
