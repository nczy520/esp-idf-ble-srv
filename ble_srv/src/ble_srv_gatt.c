#include "ble_srv_gatt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_device.h"
#include "ble_srv_wifi.h"
#include "ble_srv_led.h"
#include "ble_srv.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_system.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "BLE_SRV_GATT";

#define GATT_RESTART_CMD_DELAY_MS    100
#define GATT_RESTART_CHR_DELAY_MS    500

static SemaphoreHandle_t s_gatt_lock = NULL;

#define GATT_LOCK()   do { if (s_gatt_lock) xSemaphoreTake(s_gatt_lock, portMAX_DELAY); } while(0)
#define GATT_UNLOCK() do { if (s_gatt_lock) xSemaphoreGive(s_gatt_lock); } while(0)

static uint8_t s_partition_index = 0;
static uint8_t s_write_buf[512];

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

static const ble_uuid16_t s_srv_svc_uuid          = BLE_UUID16_INIT(BLE_SRV_SVC_UUID);
static const ble_uuid16_t s_srv_cmd_chr_uuid      = BLE_UUID16_INIT(BLE_SRV_CMD_CHAR_UUID);
static const ble_uuid16_t s_srv_info_chr_uuid     = BLE_UUID16_INIT(BLE_SRV_INFO_CHAR_UUID);
static const ble_uuid16_t s_srv_memory_chr_uuid   = BLE_UUID16_INIT(BLE_SRV_MEMORY_CHAR_UUID);
static const ble_uuid16_t s_srv_cpu_chr_uuid      = BLE_UUID16_INIT(BLE_SRV_CPU_CHAR_UUID);
static const ble_uuid16_t s_srv_flash_chr_uuid    = BLE_UUID16_INIT(BLE_SRV_FLASH_CHAR_UUID);
static const ble_uuid16_t s_srv_partition_chr_uuid = BLE_UUID16_INIT(BLE_SRV_PARTITION_CHAR_UUID);
static const ble_uuid16_t s_srv_restart_chr_uuid  = BLE_UUID16_INIT(BLE_SRV_RESTART_CHAR_UUID);

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

static int handle_read_chr(uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt)
{
    int rc;

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

    return 0;
}

int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR:
        return handle_read_chr(attr_handle, ctxt);

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0 || om_len > sizeof(s_write_buf)) {
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
    if (s_gatt_lock) {
        vSemaphoreDelete(s_gatt_lock);
        s_gatt_lock = NULL;
    }
}

bool ble_srv_gatt_init_lock(void)
{
    if (s_gatt_lock) return true;
    s_gatt_lock = xSemaphoreCreateMutex();
    return s_gatt_lock != NULL;
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
