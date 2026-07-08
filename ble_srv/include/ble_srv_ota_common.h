#ifndef BLE_SRV_OTA_COMMON_H
#define BLE_SRV_OTA_COMMON_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_OTA_SVC_UUID              0xFFD0
#define BLE_OTA_STATUS_CHAR_UUID      0xFFD3

#define BLE_OTA_MAX_FW_SIZE           (4 * 1024 * 1024)

typedef enum {
    BLE_OTA_STATE_IDLE = 0x00,
    BLE_OTA_STATE_CHECKING,
    BLE_OTA_STATE_CHECK_OK,
    BLE_OTA_STATE_CHECK_FAIL,
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
    BLE_OTA_ERR_NO_NETWORK,
} ble_ota_err_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t error_code;
    uint32_t fw_size;
    uint32_t bytes_written;
    uint8_t progress;
} ble_ota_status_t;

typedef void (*ble_srv_status_cb_t)(ble_ota_status_t *status);

void ble_srv_ota_set_state(ble_ota_state_t state, ble_ota_err_t error);
ble_ota_state_t ble_srv_ota_get_state(void);
bool ble_srv_ota_get_status(ble_ota_status_t *status);
void ble_srv_ota_push_status(void);
void ble_srv_ota_update_progress(uint32_t total_size, uint32_t bytes_written);

void ble_srv_ota_register_status_cb(ble_srv_status_cb_t cb);
void ble_srv_ota_unregister_status_cb(void);

#ifdef __cplusplus
}
#endif

#endif
