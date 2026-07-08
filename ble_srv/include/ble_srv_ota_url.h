#ifndef BLE_SRV_OTA_URL_H
#define BLE_SRV_OTA_URL_H

#include <stdint.h>
#include <stdbool.h>
#include "ble_srv_ota_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_OTA_URL_CMD_CHAR_UUID    0xFFD4
#define BLE_OTA_URL_MAX_URL_LEN      256

typedef enum {
    BLE_OTA_URL_CMD_START_URL     = 0x01,
    BLE_OTA_URL_CMD_START_DEFAULT = 0x02,
    BLE_OTA_URL_CMD_ABORT         = 0x03,
} ble_ota_url_cmd_t;

bool ble_srv_ota_url_init(void);
void ble_srv_ota_url_deinit(void);
bool ble_srv_ota_url_start(const char *url);
bool ble_srv_ota_url_start_default(void);
void ble_srv_ota_url_abort(void);
bool ble_srv_ota_url_is_running(void);

#ifdef __cplusplus
}
#endif

#endif
