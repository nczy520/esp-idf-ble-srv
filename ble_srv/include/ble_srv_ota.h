#ifndef BLE_SRV_OTA_H
#define BLE_SRV_OTA_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_OTA_SVC_UUID              0xFFD0
#define BLE_OTA_CMD_CHAR_UUID         0xFFD1
#define BLE_OTA_FW_DATA_CHAR_UUID     0xFFD2
#define BLE_OTA_STATUS_CHAR_UUID      0xFFD3

#define BLE_OTA_MAX_FW_SIZE           (4 * 1024 * 1024)

typedef enum {
    BLE_OTA_CMD_START  = 0x01,
    BLE_OTA_CMD_ABORT  = 0x02,
    BLE_OTA_CMD_VERIFY = 0x03,
    BLE_OTA_CMD_APPLY  = 0x04,
} ble_ota_cmd_t;

typedef enum {
    BLE_OTA_STATE_IDLE = 0x00,
    BLE_OTA_STATE_RECEIVING,
    BLE_OTA_STATE_VERIFYING,
    BLE_OTA_STATE_VERIFY_OK,
    BLE_OTA_STATE_VERIFY_FAIL,
    BLE_OTA_STATE_APPLYING,
    BLE_OTA_STATE_APPLY_OK,
    BLE_OTA_STATE_APPLY_FAIL,
    BLE_OTA_STATE_ERROR,
} ble_ota_state_t;

typedef enum {
    BLE_OTA_ERR_NONE = 0x00,
    BLE_OTA_ERR_INVALID_CMD,
    BLE_OTA_ERR_INVALID_SIZE,
    BLE_OTA_ERR_FLASH_WRITE,
    BLE_OTA_ERR_NO_PARTITION,
    BLE_OTA_ERR_VERIFY_FAILED,
    BLE_OTA_ERR_INTERNAL,
    BLE_OTA_ERR_BUSY,
} ble_ota_err_t;

typedef struct __attribute__((packed)) {
    uint32_t fw_size;
    uint32_t fw_crc;
    uint16_t chunk_size;
    uint16_t reserved;
    uint32_t fw_version;
} ble_ota_start_req_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t error_code;
    uint32_t fw_size;
    uint32_t bytes_written;
    uint8_t progress;
} ble_ota_status_t;

typedef void (*ble_srv_status_cb_t)(ble_ota_status_t *status);

void ble_srv_ota_reset(void);
void ble_srv_dispatch_ota_cmd(const uint8_t *data, uint16_t len);
bool ble_srv_ota_get_status(ble_ota_status_t *status);
ble_ota_state_t ble_srv_ota_get_state(void);
void ble_srv_ota_push_status(void);
bool ble_srv_ota_process_fw_data(const uint8_t *data, uint16_t len);

bool ble_srv_ota_init(void);
void ble_srv_ota_deinit(void);

void ble_srv_ota_register_status_cb(ble_srv_status_cb_t cb);
void ble_srv_ota_unregister_status_cb(void);

#ifdef __cplusplus
}
#endif

#endif
