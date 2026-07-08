#ifndef BLE_SRV_OTA_BT_H
#define BLE_SRV_OTA_BT_H

#include <stdint.h>
#include <stdbool.h>
#include "ble_srv_ota_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_OTA_BT_CMD_CHAR_UUID     0xFFD1
#define BLE_OTA_BT_FW_DATA_CHAR_UUID 0xFFD2

typedef enum {
    BLE_OTA_BT_CMD_START  = 0x01,
    BLE_OTA_BT_CMD_ABORT  = 0x02,
    BLE_OTA_BT_CMD_VERIFY = 0x03,
    BLE_OTA_BT_CMD_APPLY  = 0x04,
} ble_ota_bt_cmd_t;

typedef struct __attribute__((packed)) {
    uint32_t fw_size;
    uint32_t fw_crc;
    uint16_t chunk_size;
    uint16_t reserved;
    uint32_t fw_version;
} ble_ota_bt_start_req_t;

bool ble_srv_ota_bt_init(void);
void ble_srv_ota_bt_deinit(void);
void ble_srv_ota_bt_reset(void);
void ble_srv_ota_bt_dispatch_cmd(const uint8_t *data, uint16_t len);
bool ble_srv_ota_bt_process_fw_data(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
