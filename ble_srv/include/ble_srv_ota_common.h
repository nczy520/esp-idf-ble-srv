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
#define BLE_OTA_INVALID_GEN           0

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
    BLE_OTA_STATE_ABORTING,
    BLE_OTA_STATE_ABORTED,
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
    BLE_OTA_ERR_ABORTED,
    BLE_OTA_ERR_DISCONNECTED,
} ble_ota_err_t;

typedef enum {
    BLE_OTA_MODE_NONE = 0,
    BLE_OTA_MODE_BT,
    BLE_OTA_MODE_URL,
} ble_ota_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t error_code;
    uint32_t fw_size;
    uint32_t bytes_written;
    uint8_t progress;
} ble_ota_status_t;

typedef void (*ble_srv_status_cb_t)(ble_ota_status_t *status);

bool ble_srv_ota_init(void);
void ble_srv_ota_deinit(void);

uint8_t ble_srv_ota_begin(ble_ota_mode_t mode);
void ble_srv_ota_abort(ble_ota_err_t reason);
void ble_srv_ota_finish(uint8_t gen, ble_ota_state_t result, ble_ota_err_t error);

bool ble_srv_ota_is_abort_requested(void);
bool ble_srv_ota_is_active(void);
ble_ota_mode_t ble_srv_ota_get_mode(void);
ble_ota_state_t ble_srv_ota_get_state(void);
uint8_t ble_srv_ota_get_current_gen(void);
bool ble_srv_ota_gen_valid(uint8_t gen);

void ble_srv_ota_set_state(uint8_t gen, ble_ota_state_t state, ble_ota_err_t error);
void ble_srv_ota_set_fw_size(uint8_t gen, uint32_t fw_size);
void ble_srv_ota_report_progress(uint8_t gen, uint32_t bytes_received, uint32_t bytes_written);
void ble_srv_ota_push_status(uint8_t gen);
bool ble_srv_ota_get_status(ble_ota_status_t *status);

void ble_srv_ota_register_status_cb(ble_srv_status_cb_t cb);

#ifdef __cplusplus
}
#endif

#endif
