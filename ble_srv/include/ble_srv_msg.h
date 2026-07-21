#ifndef BLE_SRV_MSG_H
#define BLE_SRV_MSG_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MSG_NONE = 0,

    MSG_GAP_CONNECT,
    MSG_GAP_DISCONNECT,
    MSG_GAP_ADV_COMPLETE,
    MSG_GAP_SUBSCRIBE,
    MSG_GAP_MTU,

    MSG_GATT_WRITE,
    MSG_GATT_READ,

    MSG_OTA_BT_CMD,
    MSG_OTA_BT_FW_DATA,

    MSG_OTA_URL_START,
    MSG_OTA_URL_ABORT,

    MSG_OTA_RESET_TIMER,
    MSG_OTA_PUSH_STATUS,

    MSG_LED_SET_ON,
    MSG_LED_SET_RGB,
    MSG_LED_SET_EFFECT,

    MSG_SCHEDULE_RESTART,
} ble_srv_msg_type_t;

#define BLE_SRV_MSG_DATA_MAX_LEN  512

typedef struct {
    uint16_t type;
    uint16_t attr_handle;
    uint16_t conn_handle;
    uint16_t data_len;
    uint8_t  data[BLE_SRV_MSG_DATA_MAX_LEN];
} ble_srv_msg_t;

#define BLE_SRV_QUEUE_LEN     32
#define BLE_SRV_TASK_STACK    8192
#define BLE_SRV_TASK_PRIO     6
#define BLE_SRV_MSG_POOL_SIZE 8

bool ble_srv_msg_init(void);
void ble_srv_msg_deinit(void);

bool ble_srv_msg_send(ble_srv_msg_type_t type, const uint8_t *data, uint16_t data_len,
                      uint16_t attr_handle, uint16_t conn_handle, TickType_t wait);

void ble_srv_task_start(void);
void ble_srv_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif
