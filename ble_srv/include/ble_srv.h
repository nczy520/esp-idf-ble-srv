#ifndef BLE_SRV_H
#define BLE_SRV_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_wifi.h"
#include "ble_srv_led.h"
#include "ble_srv_device.h"
#include "ble_srv_temp_sensor.h"
#include "ble_srv_log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*ble_srv_custom_cmd_cb_t)(uint16_t conn_handle, const uint8_t *data, uint16_t data_len,
                                        uint8_t *resp_buf, size_t resp_buf_len, uint16_t *resp_len);

bool ble_srv_init(void);
void ble_srv_deinit(void);
bool ble_srv_is_connected(void);
void ble_srv_restart_device(void);
void ble_srv_schedule_restart(uint32_t delay_ms);
uint16_t ble_srv_get_conn_handle(void);

#ifdef __cplusplus
}
#endif

#endif
