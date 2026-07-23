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

void ble_srv_gatt_set_log_conn_handle(uint16_t conn_handle);
void ble_srv_gatt_set_log_notify_enabled(bool enabled);
void ble_srv_gatt_set_ota_status_notify_enabled(bool enabled);
void ble_srv_gatt_set_wifi_status_notify_enabled(bool enabled);
void ble_srv_gatt_set_custom_cmd_notify_enabled(bool enabled);

// per-connection 认证状态管理。
// 写入正确 PIN 后由 GATT 写回调设置 authenticated，断连时由 core 调用 release 释放。
// 返回指定 conn_handle 的当前认证状态，未在槽位内时返回 false。
bool ble_srv_gatt_is_authenticated(uint16_t conn_handle);
// 占用 conn_handle 对应的认证槽位（每次新连接调用一次）。返回是否成功。
bool ble_srv_gatt_acquire_auth_slot(uint16_t conn_handle);
// 释放 conn_handle 对应的认证槽位；该连接再次建立时需要重新走 PIN 校验。
void ble_srv_gatt_release_auth_slot(uint16_t conn_handle);

uint16_t ble_srv_gatt_get_ota_status_chr_val_handle(void);
uint16_t ble_srv_gatt_get_wifi_status_chr_val_handle(void);
uint16_t ble_srv_gatt_get_log_chr_val_handle(void);
uint16_t ble_srv_gatt_get_custom_cmd_chr_val_handle(void);
bool ble_srv_gatt_get_ota_status_notify_enabled(void);

#ifdef __cplusplus
}
#endif

#endif
