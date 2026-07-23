#ifndef BLE_SRV_CORE_H
#define BLE_SRV_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_srv_core_handle_connect(uint16_t conn_handle);
void ble_srv_core_handle_disconnect(uint16_t conn_handle);
void ble_srv_core_handle_adv_complete(void);
void ble_srv_core_handle_subscribe(uint16_t attr_handle, uint16_t conn_handle,
                                    const uint8_t *data, uint16_t data_len);
void ble_srv_core_handle_mtu(uint16_t conn_handle, uint16_t mtu);
void ble_srv_schedule_restart_internal(uint32_t delay_ms);

// 返回当前连接协商后的 ATT MTU，未连接时为默认值 23。
// GATT notify / 读 payload / 分片逻辑可以参考此值选择单次最大长度。
uint16_t ble_srv_core_get_mtu(void);

#ifdef __cplusplus
}
#endif

#endif
