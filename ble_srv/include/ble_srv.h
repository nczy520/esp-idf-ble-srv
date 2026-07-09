#ifndef BLE_SRV_H
#define BLE_SRV_H

#include <stdbool.h>

#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_wifi.h"
#include "ble_srv_led.h"
#include "ble_srv_device.h"
#include "ble_srv_temp_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

bool ble_srv_init(void);
void ble_srv_deinit(void);
bool ble_srv_is_connected(void);
void ble_srv_restart_device(void);

#ifdef __cplusplus
}
#endif

#endif
