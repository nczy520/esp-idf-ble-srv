#include "ble_srv_gatt.h"
#include "ble_srv_ota.h"
#include "ble_srv_device.h"
#include "ble_srv_wifi.h"
#include "ble_srv_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "host/ble_hs.h"
#include "host/ble_gatt.h"

static const char *TAG = "BLE_SRV_GATT";

static uint8_t s_partition_index = 0;

uint16_t g_srv_cmd_chr_val_handle = 0;
uint16_t g_srv_info_chr_val_handle = 0;
uint16_t g_srv_memory_chr_val_handle = 0;
uint16_t g_srv_cpu_chr_val_handle = 0;
uint16_t g_srv_flash_chr_val_handle = 0;
uint16_t g_srv_partition_chr_val_handle = 0;
uint16_t g_srv_restart_chr_val_handle = 0;

uint16_t g_ota_cmd_chr_val_handle = 0;
uint16_t g_ota_fw_data_chr_val_handle = 0;
uint16_t g_ota_status_chr_val_handle = 0;
bool g_ota_status_notify_enabled = false;

uint16_t g_wifi_config_chr_val_handle = 0;
uint16_t g_wifi_status_chr_val_handle = 0;
uint16_t g_wifi_ctrl_chr_val_handle = 0;
bool g_wifi_status_notify_enabled = false;

#ifdef CONFIG_BLE_SRV_LED
uint16_t g_led_ctrl_chr_val_handle = 0;
uint16_t g_led_color_chr_val_handle = 0;
uint16_t g_led_effect_chr_val_handle = 0;
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
static const ble_uuid16_t s_ota_cmd_chr_uuid      = BLE_UUID16_INIT(BLE_OTA_CMD_CHAR_UUID);
static const ble_uuid16_t s_ota_fw_data_chr_uuid  = BLE_UUID16_INIT(BLE_OTA_FW_DATA_CHAR_UUID);
static const ble_uuid16_t s_ota_status_chr_uuid   = BLE_UUID16_INIT(BLE_OTA_STATUS_CHAR_UUID);

static const ble_uuid16_t s_wifi_svc_uuid          = BLE_UUID16_INIT(BLE_WIFI_SVC_UUID);
static const ble_uuid16_t s_wifi_config_chr_uuid   = BLE_UUID16_INIT(BLE_WIFI_CONFIG_CHAR_UUID);
static const ble_uuid16_t s_wifi_status_chr_uuid   = BLE_UUID16_INIT(BLE_WIFI_STATUS_CHAR_UUID);
static const ble_uuid16_t s_wifi_ctrl_chr_uuid     = BLE_UUID16_INIT(BLE_WIFI_CTRL_CHAR_UUID);

#ifdef CONFIG_BLE_SRV_LED
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
                .val_handle = &g_srv_cmd_chr_val_handle,
            },
            {
                .uuid = &s_srv_info_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &g_srv_info_chr_val_handle,
            },
            {
                .uuid = &s_srv_memory_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &g_srv_memory_chr_val_handle,
            },
            {
                .uuid = &s_srv_cpu_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &g_srv_cpu_chr_val_handle,
            },
            {
                .uuid = &s_srv_flash_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &g_srv_flash_chr_val_handle,
            },
            {
                .uuid = &s_srv_partition_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_srv_partition_chr_val_handle,
            },
            {
                .uuid = &s_srv_restart_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &g_srv_restart_chr_val_handle,
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_ota_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_ota_cmd_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &g_ota_cmd_chr_val_handle,
            },
            {
                .uuid = &s_ota_fw_data_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &g_ota_fw_data_chr_val_handle,
            },
            {
                .uuid = &s_ota_status_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_ota_status_chr_val_handle,
            },
            { 0 },
        },
    },
#ifdef CONFIG_BLE_SRV_WIFI_PROVISIONER
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_wifi_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_wifi_config_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_wifi_config_chr_val_handle,
            },
            {
                .uuid = &s_wifi_status_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &g_wifi_status_chr_val_handle,
            },
            {
                .uuid = &s_wifi_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &g_wifi_ctrl_chr_val_handle,
            },
            { 0 },
        },
    },
#endif
#ifdef CONFIG_BLE_SRV_LED
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_led_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_led_ctrl_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &g_led_ctrl_chr_val_handle,
            },
            {
                .uuid = &s_led_color_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
                .val_handle = &g_led_color_chr_val_handle,
            },
            {
                .uuid = &s_led_effect_chr_uuid.u,
                .access_cb = ble_srv_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
                .val_handle = &g_led_effect_chr_val_handle,
            },
            { 0 },
        },
    },
#endif
    { 0 },
};

int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    int rc;

    switch (ctxt->op) {
    case BLE_GATT_ACCESS_OP_READ_CHR: {
        if (attr_handle == g_srv_info_chr_val_handle) {
            ble_srv_device_info_t info;
            if (ble_srv_get_device_info(&info)) {
                rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_srv_memory_chr_val_handle) {
            ble_srv_memory_info_t info;
            if (ble_srv_get_memory_info(&info)) {
                rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_srv_cpu_chr_val_handle) {
            ble_srv_cpu_info_t info;
            if (ble_srv_get_cpu_info(&info)) {
                rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_srv_flash_chr_val_handle) {
            ble_srv_flash_info_t info;
            if (ble_srv_get_flash_info(&info)) {
                rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_srv_partition_chr_val_handle) {
            ble_srv_partition_info_t info;
            if (ble_srv_get_partition_info(s_partition_index, &info)) {
                rc = os_mbuf_append(ctxt->om, &info, sizeof(info));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_ota_status_chr_val_handle) {
            ble_ota_status_t status;
            if (ble_srv_ota_get_status(&status)) {
                rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
#ifdef CONFIG_BLE_SRV_WIFI_PROVISIONER
        else if (attr_handle == g_wifi_status_chr_val_handle) {
            ble_wifi_status_t status;
            if (ble_srv_wifi_get_status(&status)) {
                rc = os_mbuf_append(ctxt->om, &status, sizeof(status));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        }
#endif
#ifdef CONFIG_BLE_SRV_LED
        else if (attr_handle == g_led_ctrl_chr_val_handle) {
            ble_led_status_t status;
            if (ble_srv_led_get_status(&status)) {
                rc = os_mbuf_append(ctxt->om, &status.on, sizeof(status.on));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_led_color_chr_val_handle) {
            ble_led_status_t status;
            if (ble_srv_led_get_status(&status)) {
                uint8_t rgb[3] = {status.red, status.green, status.blue};
                rc = os_mbuf_append(ctxt->om, rgb, sizeof(rgb));
                return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return BLE_ATT_ERR_UNLIKELY;
        } else if (attr_handle == g_led_effect_chr_val_handle) {
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

    case BLE_GATT_ACCESS_OP_WRITE_CHR: {
        uint16_t om_len = OS_MBUF_PKTLEN(ctxt->om);
        if (om_len == 0) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }

        uint8_t *buf = malloc(om_len);
        if (!buf) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }

        uint16_t data_len = 0;
        rc = ble_hs_mbuf_to_flat(ctxt->om, buf, om_len, &data_len);
        if (rc != 0) {
            free(buf);
            return BLE_ATT_ERR_UNLIKELY;
        }

        if (attr_handle == g_srv_cmd_chr_val_handle) {
            ble_srv_cmd_t cmd = (ble_srv_cmd_t)buf[0];
            ESP_LOGI(TAG, "SRV command: 0x%02X", cmd);
            switch (cmd) {
            case BLE_SRV_CMD_RESTART:
                vTaskDelay(pdMS_TO_TICKS(100));
                esp_restart();
                break;
            default:
                break;
            }
        } else if (attr_handle == g_ota_cmd_chr_val_handle) {
            ble_srv_dispatch_ota_cmd(buf, data_len);
        } else if (attr_handle == g_ota_fw_data_chr_val_handle) {
            ble_srv_ota_process_fw_data(buf, data_len);
        } else if (attr_handle == g_srv_partition_chr_val_handle) {
            if (data_len > 0) {
                s_partition_index = buf[0];
            }
        } else if (attr_handle == g_srv_restart_chr_val_handle) {
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
#ifdef CONFIG_BLE_SRV_WIFI_PROVISIONER
        else if (attr_handle == g_wifi_config_chr_val_handle) {
            if (data_len >= sizeof(ble_wifi_config_t)) {
                const ble_wifi_config_t *wifi_cfg = (const ble_wifi_config_t *)buf;
                ble_srv_wifi_connect(wifi_cfg->ssid, wifi_cfg->password);
            }
        } else if (attr_handle == g_wifi_ctrl_chr_val_handle) {
            ble_wifi_ctrl_cmd_t cmd = (ble_wifi_ctrl_cmd_t)buf[0];
            switch (cmd) {
            case BLE_WIFI_CTRL_FORGET:
                ble_srv_wifi_forget();
                break;
            case BLE_WIFI_CTRL_NTP_SYNC:
#ifdef CONFIG_BLE_SRV_NTP_SYNC
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
#ifdef CONFIG_BLE_SRV_LED
        else if (attr_handle == g_led_ctrl_chr_val_handle) {
            ble_led_ctrl_t ctrl = (ble_led_ctrl_t)buf[0];
            if (ctrl == BLE_LED_CTRL_ON) {
                ble_srv_led_set_on(true);
            } else if (ctrl == BLE_LED_CTRL_OFF) {
                ble_srv_led_set_on(false);
            }
        } else if (attr_handle == g_led_color_chr_val_handle) {
            if (data_len >= 3) {
                ble_srv_led_set_rgb(buf[0], buf[1], buf[2]);
            }
        } else if (attr_handle == g_led_effect_chr_val_handle) {
            if (data_len >= sizeof(ble_led_effect_config_t)) {
                const ble_led_effect_config_t *cfg = (const ble_led_effect_config_t *)buf;
                ble_srv_led_set_effect((ble_led_effect_t)cfg->effect, cfg->speed);
            } else if (data_len >= 1) {
                ble_srv_led_set_effect((ble_led_effect_t)buf[0], 50);
            }
        }
#endif

        free(buf);
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
