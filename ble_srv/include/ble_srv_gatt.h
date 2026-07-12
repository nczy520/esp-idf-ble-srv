#ifndef BLE_SRV_GATT_H
#define BLE_SRV_GATT_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

extern uint16_t g_srv_cmd_chr_val_handle;
extern uint16_t g_srv_info_chr_val_handle;
extern uint16_t g_srv_memory_chr_val_handle;
extern uint16_t g_srv_cpu_chr_val_handle;
extern uint16_t g_srv_flash_chr_val_handle;
extern uint16_t g_srv_partition_chr_val_handle;
extern uint16_t g_srv_restart_chr_val_handle;

extern uint16_t g_ota_bt_cmd_chr_val_handle;
extern uint16_t g_ota_bt_fw_data_chr_val_handle;
extern uint16_t g_ota_status_chr_val_handle;
extern bool g_ota_status_notify_enabled;

#ifdef CONFIG_BLE_SRV_OTA_URL_ENABLED
extern uint16_t g_ota_url_chr_val_handle;
#endif

extern uint16_t g_wifi_config_chr_val_handle;
extern uint16_t g_wifi_status_chr_val_handle;
extern uint16_t g_wifi_ctrl_chr_val_handle;
extern bool g_wifi_status_notify_enabled;

#ifdef CONFIG_BLE_SRV_LED_ENABLED
extern uint16_t g_led_ctrl_chr_val_handle;
extern uint16_t g_led_color_chr_val_handle;
extern uint16_t g_led_effect_chr_val_handle;
#endif

int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);

const struct ble_gatt_svc_def *ble_srv_get_gatt_svcs(void);
void ble_srv_gatt_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
