#include "ble_srv_gatt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_device.h"
#include "ble_srv_wifi.h"
#include "ble_srv_led.h"
#include "ble_srv_log.h"
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

static SemaphoreHandle_t s_gatt_lock = NULL;

#define GATT_LOCK()   do { if (s_gatt_lock) xSemaphoreTake(s_gatt_lock, portMAX_DELAY); } while(0)
#define GATT_UNLOCK() do { if (s_gatt_lock) xSemaphoreGive(s_gatt_lock); } while(0)

static uint8_t s_partition_index = 0;
#define BLE_SRV_GATT_WRITE_BUF_SIZE 512
#define BLE_SRV_GATT_SELECTED_FILE_SIZE 64
static uint8_t *s_write_buf = NULL;

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

static uint16_t s_srv_log_file_list_chr_val_handle = 0;
static uint16_t s_srv_log_file_content_chr_val_handle = 0;
static uint16_t s_srv_log_file_download_chr_val_handle = 0;
static uint16_t s_srv_log_http_ctrl_chr_val_handle = 0;
static char *s_selected_log_file = NULL;

static const ble_uuid16_t s_srv_svc_uuid          = BLE_UUID16_INIT(BLE_SRV_SVC_UUID);
static const ble_uuid16_t s_srv_cmd_chr_uuid      = BLE_UUID16_INIT(BLE_SRV_CMD_CHAR_UUID);
static const ble_uuid16_t s_srv_info_chr_uuid     = BLE_UUID16_INIT(BLE_SRV_INFO_CHAR_UUID);
static const ble_uuid16_t s_srv_memory_chr_uuid   = BLE_UUID16_INIT(BLE_SRV_MEMORY_CHAR_UUID);
static const ble_uuid16_t s_srv_cpu_chr_uuid      = BLE_UUID16_INIT(BLE_SRV_CPU_CHAR_UUID);
static const ble_uuid16_t s_srv_flash_chr_uuid    = BLE_UUID16_INIT(BLE_SRV_FLASH_CHAR_UUID);
static const ble_uuid16_t s_srv_partition_chr_uuid = BLE_UUID16_INIT(BLE_SRV_PARTITION_CHAR_UUID);
static const ble_uuid16_t s_srv_restart_chr_uuid  = BLE_UUID16_INIT(BLE_SRV_RESTART_CHAR_UUID);

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
static const ble_uuid16_t s_srv_auth_chr_uuid      = BLE_UUID16_INIT(BLE_SRV_AUTH_CHAR_UUID);
#endif

static const ble_uuid16_t s_srv_log_chr_uuid       = BLE_UUID16_INIT(BLE_SRV_LOG_CHAR_UUID);
static const ble_uuid16_t s_srv_custom_cmd_chr_uuid = BLE_UUID16_INIT(BLE_SRV_CUSTOM_CMD_CHAR_UUID);
static const ble_uuid16_t s_srv_log_file_list_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_FILE_LIST_CHAR_UUID);
static const ble_uuid16_t s_srv_log_file_content_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_FILE_CONTENT_CHAR_UUID);
static const ble_uuid16_t s_srv_log_file_download_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_FILE_DOWNLOAD_CHAR_UUID);
static const ble_uuid16_t s_srv_log_http_ctrl_chr_uuid = BLE_UUID16_INIT(BLE_SRV_LOG_HTTP_CTRL_CHAR_UUID);

static const ble_uuid16_t s_ota_svc_uuid          = BLE_UUID16_INIT(BLE_OTA_SVC_UUID);
static const ble_uuid16_t s_ota_bt_cmd_chr_uuid   = BLE_UUID16_INIT(BLE_OTA_BT_CMD_CHAR_UUID);
static const ble_uuid16_t s_ota_bt_fw_data_chr_uuid = BLE_UUID16_INIT(BLE_OTA_BT_FW_DATA_CHAR_UUID);
static const ble_uuid16_t s_ota_status_chr_uuid   = BLE_UUID16_INIT(BLE_OTA_STATUS_CHAR_UUID);

#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
static const ble_uuid16_t s_ota_url_chr_uuid      = BLE_UUID16_INIT(BLE_OTA_URL_CMD_CHAR_UUID);
#endif

static const ble_uuid16_t s_wifi_svc_uuid          = BLE_UUID16_INIT(BLE_WIFI_SVC_UUID);
static const ble_uuid16_t s_wifi_config_chr_uuid   = BLE_UUID16_INIT(BLE_WIFI_CONFIG_CHAR_UUID);
static const ble_uuid16_t s_wifi_status_chr_uuid   = BLE_UUID16_INIT(BLE_WIFI_STATUS_CHAR_UUID);
static const ble_uuid16_t s_wifi_ctrl_chr_uuid     = BLE_UUID16_INIT(BLE_WIFI_CTRL_CHAR_UUID);

#ifdef CONFIG_BLE_SRV_LED_ENABLED
static const ble_uuid16_t s_led_svc_uuid           = BLE_UUID16_INIT(BLE_LED_SVC_UUID);
static const ble_uuid16_t s_led_ctrl_chr_uuid      = BLE_UUID16_INIT(BLE_LED_CTRL_CHAR_UUID);
static const ble_uuid16_t s_led_color_chr_uuid     = BLE_UUID16_INIT(BLE_LED_COLOR_CHAR_UUID);
static const ble_uuid16_t s_led_effect_chr_uuid    = BLE_UUID16_INIT(BLE_LED_EFFECT_CHAR_UUID);
#endif

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
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_restart_chr_val_handle,
            },
#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
            {
                .uuid = &s_srv_auth_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_auth_chr_val_handle,
            },
#endif
            {
                .uuid = &s_srv_log_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_log_chr_val_handle,
            },
            {
                .uuid = &s_srv_custom_cmd_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_custom_cmd_chr_val_handle,
            },
            {
                .uuid = &s_srv_log_file_list_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_srv_log_file_list_chr_val_handle,
            },
            {
                .uuid = &s_srv_log_file_content_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_srv_log_file_content_chr_val_handle,
            },
            {
                .uuid = &s_srv_log_file_download_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_srv_log_file_download_chr_val_handle,
            },
            {
                .uuid = &s_srv_log_http_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_srv_log_http_ctrl_chr_val_handle,
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
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_wifi_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_wifi_config_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
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
#endif
#ifdef CONFIG_BLE_SRV_LED_ENABLED
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_led_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_led_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
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
    } else if (attr_handle == s_srv_log_file_list_chr_val_handle) {
        ble_srv_log_file_info_t *files = heap_caps_malloc(sizeof(ble_srv_log_file_info_t) * BLE_SRV_LOG_FILE_LIST_MAX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!files) files = heap_caps_malloc(sizeof(ble_srv_log_file_info_t) * BLE_SRV_LOG_FILE_LIST_MAX, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!files) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        int count = ble_srv_log_get_file_list(files, BLE_SRV_LOG_FILE_LIST_MAX);
        if (count > 0) {
            uint8_t count_byte = (uint8_t)count;
            rc = os_mbuf_append(ctxt->om, &count_byte, sizeof(count_byte));
            if (rc != 0) { heap_caps_free(files); return BLE_ATT_ERR_INSUFFICIENT_RES; }
            for (int i = 0; i < count; i++) {
                rc = os_mbuf_append(ctxt->om, &files[i], sizeof(files[i]));
                if (rc != 0) { heap_caps_free(files); return BLE_ATT_ERR_INSUFFICIENT_RES; }
            }
            heap_caps_free(files);
            return 0;
        }
        heap_caps_free(files);
        uint8_t zero = 0;
        rc = os_mbuf_append(ctxt->om, &zero, sizeof(zero));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (attr_handle == s_srv_log_file_content_chr_val_handle) {
        if (s_selected_log_file[0]) {
            char *buffer = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buffer) buffer = heap_caps_malloc(1024, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (!buffer) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            int read_len = ble_srv_log_read_file(s_selected_log_file, buffer, 1024);
            if (read_len >= 0) {
                if (read_len == 0) {
                    const char *empty_msg = "(empty file)";
                    rc = os_mbuf_append(ctxt->om, empty_msg, strlen(empty_msg));
                } else {
                    rc = os_mbuf_append(ctxt->om, buffer, read_len);
                }
                heap_caps_free(buffer);
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            heap_caps_free(buffer);
            ESP_LOGE(TAG, "Failed to read log file: %s", s_selected_log_file);
            BLE_SRV_LOGE(TAG, "Failed to read log file: %s", s_selected_log_file);
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

static int handle_write_chr(uint16_t conn_handle, uint16_t attr_handle,
                             uint8_t *data, uint16_t data_len)
{
#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
    if (attr_handle == s_srv_auth_chr_val_handle) {
        if (data_len == 0 || data_len > BLE_SRV_AUTH_PIN_MAX_LEN) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        GATT_LOCK();
        bool match = (strlen(s_auth_pin) == data_len &&
                      memcmp(data, s_auth_pin, data_len) == 0);
        s_conn_authenticated = match;
        GATT_UNLOCK();
        if (match) {
            ESP_LOGI(TAG, "PIN authentication success (conn=%d)", conn_handle);
            BLE_SRV_LOGI(TAG, "PIN authentication success (conn=%d)", conn_handle);
        } else {
            ESP_LOGW(TAG, "PIN authentication failed (conn=%d)", conn_handle);
            BLE_SRV_LOGW(TAG, "PIN authentication failed (conn=%d)", conn_handle);
        }
        return match ? 0 : BLE_ATT_ERR_UNLIKELY;
    }
    if (!s_conn_authenticated) {
        ESP_LOGW(TAG, "Write denied: not authenticated (handle=%d)", attr_handle);
        ble_srv_auth_fail_disconnect(conn_handle);
        return BLE_SRV_AUTH_ERR_NOT_AUTH;
    }
#endif

    if (attr_handle == s_ota_bt_cmd_chr_val_handle) {
        if (data_len < 1) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        ble_ota_bt_cmd_t bt_cmd = (ble_ota_bt_cmd_t)data[0];
        if (bt_cmd == BLE_OTA_BT_CMD_START && data_len < 1 + (int)sizeof(ble_ota_bt_start_req_t)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        bool ok = ble_srv_ota_bt_dispatch_cmd(data, data_len);
        if (!ok && bt_cmd == BLE_OTA_BT_CMD_START) {
            return BLE_ATT_ERR_UNLIKELY;
        }
    }
#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
    else if (attr_handle == s_ota_url_chr_val_handle) {
        if (data_len < 1) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        ble_ota_url_cmd_t cmd = (ble_ota_url_cmd_t)data[0];
        switch (cmd) {
        case BLE_OTA_URL_CMD_START_URL:
            if (data_len > 1) {
                uint16_t url_len = data_len - 1;
                if (url_len > BLE_OTA_URL_MAX_URL_LEN) {
                    url_len = BLE_OTA_URL_MAX_URL_LEN;
                }
                char url[BLE_OTA_URL_MAX_URL_LEN + 1] = {0};
                memcpy(url, data + 1, url_len);
                url[url_len] = '\0';
                if (!ble_srv_ota_url_start(url)) {
                    return BLE_ATT_ERR_UNLIKELY;
                }
            }
            break;
        case BLE_OTA_URL_CMD_START_DEFAULT: {
            const char *default_url = CONFIG_BLE_SRV_OTA_URL_DEFAULT;
            if (strlen(default_url) == 0) {
                ESP_LOGE(TAG, "Default OTA URL is empty");
                return BLE_ATT_ERR_UNLIKELY;
            }
            if (!ble_srv_ota_url_start(default_url)) {
                return BLE_ATT_ERR_UNLIKELY;
            }
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
            if (data_len < (uint16_t)(1 + ssid_len + 1)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
            uint8_t pass_len = data[1 + ssid_len];
            if (pass_len > 64) pass_len = 64;
            if (data_len < (uint16_t)(1 + ssid_len + 1 + pass_len)) {
                return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
            }
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
    } else if (attr_handle == s_srv_log_file_content_chr_val_handle) {
        if (data_len > 0 && data_len <= BLE_SRV_GATT_SELECTED_FILE_SIZE - 1) {
            memset(s_selected_log_file, 0, BLE_SRV_GATT_SELECTED_FILE_SIZE);
            memcpy(s_selected_log_file, data, data_len);
            ESP_LOGI(TAG, "Selected log file: %s", s_selected_log_file);
        }
    } else if (attr_handle == s_srv_log_file_download_chr_val_handle) {
        if (s_selected_log_file[0]) {
            char *buffer = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!buffer) buffer = heap_caps_malloc(512, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
            if (buffer) {
                int read_len = ble_srv_log_read_file(s_selected_log_file, buffer, 512);
                if (read_len > 0) {
                    struct os_mbuf *om = ble_hs_mbuf_from_flat(buffer, read_len);
                    if (om) {
                        ble_gatts_notify_custom(conn_handle, s_srv_log_file_download_chr_val_handle, om);
                    }
                }
                heap_caps_free(buffer);
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
            }
        }
    }

    return 0;
}

int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        return handle_read_chr(conn_handle, attr_handle, ctxt);

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > BLE_SRV_GATT_WRITE_BUF_SIZE) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint16_t data_len = 0;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, s_write_buf, om_len, &data_len);
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }

        return handle_write_chr(conn_handle, attr_handle, s_write_buf, data_len);
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
    if (s_selected_log_file) {
        heap_caps_free(s_selected_log_file);
        s_selected_log_file = NULL;
    }
    if (s_gatt_lock) {
        vSemaphoreDelete(s_gatt_lock);
        s_gatt_lock = NULL;
    }
}

bool ble_srv_gatt_init_lock(void)
{
    if (s_gatt_lock) return true;
    s_gatt_lock = xSemaphoreCreateMutex();
    if (!s_gatt_lock) return false;

    s_write_buf = heap_caps_malloc(BLE_SRV_GATT_WRITE_BUF_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_write_buf) s_write_buf = heap_caps_malloc(BLE_SRV_GATT_WRITE_BUF_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_write_buf) { vSemaphoreDelete(s_gatt_lock); s_gatt_lock = NULL; return false; }

    s_selected_log_file = heap_caps_malloc(BLE_SRV_GATT_SELECTED_FILE_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_selected_log_file) s_selected_log_file = heap_caps_malloc(BLE_SRV_GATT_SELECTED_FILE_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_selected_log_file) { heap_caps_free(s_write_buf); s_write_buf = NULL; vSemaphoreDelete(s_gatt_lock); s_gatt_lock = NULL; return false; }
    memset(s_selected_log_file, 0, BLE_SRV_GATT_SELECTED_FILE_SIZE);

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
    GATT_LOCK();
    bool en = s_ota_status_notify_enabled;
    GATT_UNLOCK();
    return en;
}

void ble_srv_gatt_set_ota_status_notify_enabled(bool enabled)
{
    GATT_LOCK();
    s_ota_status_notify_enabled = enabled;
    GATT_UNLOCK();
}

uint16_t ble_srv_gatt_get_ota_status_chr_val_handle(void)
{
    GATT_LOCK();
    uint16_t h = s_ota_status_chr_val_handle;
    GATT_UNLOCK();
    return h;
}

bool ble_srv_gatt_get_wifi_status_notify_enabled(void)
{
    GATT_LOCK();
    bool en = s_wifi_status_notify_enabled;
    GATT_UNLOCK();
    return en;
}

void ble_srv_gatt_set_wifi_status_notify_enabled(bool enabled)
{
    GATT_LOCK();
    s_wifi_status_notify_enabled = enabled;
    GATT_UNLOCK();
}

uint16_t ble_srv_gatt_get_wifi_status_chr_val_handle(void)
{
    GATT_LOCK();
    uint16_t h = s_wifi_status_chr_val_handle;
    GATT_UNLOCK();
    return h;
}

#ifdef CONFIG_BLE_SRV_AUTH_ENABLED
bool ble_srv_gatt_is_auth_enabled(void)
{
    return true;
}

bool ble_srv_gatt_is_conn_authenticated(uint16_t conn_handle)
{
    (void)conn_handle;
    return s_conn_authenticated;
}

void ble_srv_gatt_set_conn_authenticated(uint16_t conn_handle, bool authed)
{
    (void)conn_handle;
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
    GATT_LOCK();
    uint16_t h = s_srv_auth_chr_val_handle;
    GATT_UNLOCK();
    return h;
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
    GATT_LOCK();
    bool en = s_log_notify_enabled;
    GATT_UNLOCK();
    return en;
}

void ble_srv_gatt_set_log_notify_enabled(bool enabled)
{
    GATT_LOCK();
    s_log_notify_enabled = enabled;
    GATT_UNLOCK();
}

uint16_t ble_srv_gatt_get_log_chr_val_handle(void)
{
    GATT_LOCK();
    uint16_t h = s_srv_log_chr_val_handle;
    GATT_UNLOCK();
    return h;
}

static uint16_t s_log_conn_handle = 0;

void ble_srv_gatt_set_log_conn_handle(uint16_t conn_handle)
{
    s_log_conn_handle = conn_handle;
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

    static char line[BLE_SRV_LOG_MAX_LEN + 8];
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

    static char body[BLE_SRV_LOG_MAX_LEN];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }

    static char line[BLE_SRV_LOG_MAX_LEN + 32];
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

uint16_t ble_srv_gatt_get_custom_cmd_chr_val_handle(void)
{
    GATT_LOCK();
    uint16_t h = s_srv_custom_cmd_chr_val_handle;
    GATT_UNLOCK();
    return h;
}

bool ble_srv_gatt_custom_cmd_notify_enabled(void)
{
    GATT_LOCK();
    bool en = s_custom_cmd_notify_enabled;
    GATT_UNLOCK();
    return en;
}

void ble_srv_gatt_set_custom_cmd_notify_enabled(bool enabled)
{
    GATT_LOCK();
    s_custom_cmd_notify_enabled = enabled;
    GATT_UNLOCK();
}

void ble_srv_gatt_set_custom_cmd_callback(ble_srv_custom_cmd_cb_t cb)
{
    GATT_LOCK();
    s_custom_cmd_cb = cb;
    GATT_UNLOCK();
}

bool ble_srv_gatt_custom_cmd_notify(uint16_t conn_handle, const uint8_t *data, uint16_t data_len)
{
    if (!s_custom_cmd_notify_enabled || s_srv_custom_cmd_chr_val_handle == 0) {
        return false;
    }
    if (!data || data_len == 0) {
        return false;
    }
    if (conn_handle == 0 || conn_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, data_len);
    if (!om) {
        return false;
    }
    int rc = ble_gatts_notify_custom(conn_handle, s_srv_custom_cmd_chr_val_handle, om);
    return rc == 0;
}
