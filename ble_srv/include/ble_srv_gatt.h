#ifndef BLE_SRV_GATT_H
#define BLE_SRV_GATT_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_gatt.h"
#include "ble_srv_log.h"

#ifdef __cplusplus
extern "C" {
#endif

int ble_srv_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg);

const struct ble_gatt_svc_def *ble_srv_get_gatt_svcs(void);
void ble_srv_gatt_deinit(void);
bool ble_srv_gatt_init_lock(void);

uint16_t ble_srv_gatt_get_ota_status_chr_val_handle(void);
bool ble_srv_gatt_get_ota_status_notify_enabled(void);
void ble_srv_gatt_set_ota_status_notify_enabled(bool enabled);

uint16_t ble_srv_gatt_get_wifi_status_chr_val_handle(void);
bool ble_srv_gatt_get_wifi_status_notify_enabled(void);
void ble_srv_gatt_set_wifi_status_notify_enabled(bool enabled);

bool ble_srv_gatt_is_auth_enabled(void);
bool ble_srv_gatt_is_conn_authenticated(uint16_t conn_handle);
void ble_srv_gatt_set_conn_authenticated(uint16_t conn_handle, bool authed);
void ble_srv_gatt_clear_auth_state(uint16_t conn_handle);
uint16_t ble_srv_gatt_get_auth_chr_val_handle(void);

bool ble_srv_gatt_log_notify_enabled(void);
void ble_srv_gatt_set_log_notify_enabled(bool enabled);
uint16_t ble_srv_gatt_get_log_chr_val_handle(void);
void ble_srv_gatt_set_log_conn_handle(uint16_t conn_handle);
void ble_srv_gatt_log_send(ble_srv_log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void ble_srv_gatt_log_send_raw(ble_srv_log_level_t level, const char *msg);

typedef int (*ble_srv_custom_cmd_cb_t)(uint16_t conn_handle, const uint8_t *data, uint16_t data_len,
                                        uint8_t *resp_buf, uint16_t resp_buf_size, uint16_t *out_resp_len);

uint16_t ble_srv_gatt_get_custom_cmd_chr_val_handle(void);
bool ble_srv_gatt_custom_cmd_notify_enabled(void);
void ble_srv_gatt_set_custom_cmd_notify_enabled(bool enabled);
void ble_srv_gatt_set_custom_cmd_callback(ble_srv_custom_cmd_cb_t cb);
bool ble_srv_gatt_custom_cmd_notify(uint16_t conn_handle, const uint8_t *data, uint16_t data_len);

#ifdef __cplusplus
}
#endif

#endif
