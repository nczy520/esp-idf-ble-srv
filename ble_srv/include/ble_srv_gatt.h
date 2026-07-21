#ifndef BLE_SRV_GATT_H
#define BLE_SRV_GATT_H

#include <stdint.h>
#include <stdbool.h>
#include "ble_srv_log.h"
#include "host/ble_gatt.h"

#ifdef __cplusplus
extern "C" {
#endif

void ble_srv_gatt_handle_write(uint16_t conn_handle, uint16_t attr_handle,
                               const uint8_t *data, uint16_t data_len);
void ble_srv_gatt_handle_read(uint16_t conn_handle, uint16_t attr_handle);
void ble_srv_gatt_log_send(ble_srv_log_level_t level, const char *tag, const char *fmt, ...);

const struct ble_gatt_svc_def *ble_srv_get_gatt_svcs(void);
bool ble_srv_gatt_init(void);
void ble_srv_gatt_deinit(void);

void ble_srv_gatt_clear_auth_state(uint16_t conn_handle);
void ble_srv_gatt_set_log_conn_handle(uint16_t conn_handle);
void ble_srv_gatt_set_log_notify_enabled(bool enabled);
void ble_srv_gatt_set_ota_status_notify_enabled(bool enabled);
void ble_srv_gatt_set_wifi_status_notify_enabled(bool enabled);
void ble_srv_gatt_set_custom_cmd_notify_enabled(bool enabled);

uint16_t ble_srv_gatt_get_ota_status_chr_val_handle(void);
uint16_t ble_srv_gatt_get_wifi_status_chr_val_handle(void);
uint16_t ble_srv_gatt_get_log_chr_val_handle(void);
uint16_t ble_srv_gatt_get_custom_cmd_chr_val_handle(void);
bool ble_srv_gatt_get_ota_status_notify_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
