#ifndef BLE_SRV_GATT_H
#define BLE_SRV_GATT_H

#include <stdint.h>
#include <stdbool.h>
#include "host/ble_gatt.h"

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

#ifdef __cplusplus
}
#endif

#endif
