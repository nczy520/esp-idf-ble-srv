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

// 自定义命令回调：客户端写入自定义命令特征(0xFFEA)时被调用。
// conn_handle: 当前连接句柄；data/data_len: 客户端写入的原始数据。
// resp_buf/resp_buf_len: 响应缓冲区及容量；resp_len: 实际响应长度（0 表示无响应）。
// 返回 0 表示处理成功（若有响应数据则通过 NOTIFY 发送给客户端）；非 0 表示失败，不发送响应。
typedef int (*ble_srv_custom_cmd_cb_t)(uint16_t conn_handle, const uint8_t *data, uint16_t data_len,
                                        uint8_t *resp_buf, size_t resp_buf_len, uint16_t *resp_len);

bool ble_srv_init(void);
void ble_srv_deinit(void);
bool ble_srv_is_connected(void);
void ble_srv_restart_device(void);
void ble_srv_schedule_restart(uint32_t delay_ms);
uint16_t ble_srv_get_conn_handle(void);

// === 自定义命令 API ===
// 注册自定义命令处理回调。传入 NULL 可取消注册。
// 回调运行在 ble_srv 任务线程，可安全执行较重操作（但避免长时间阻塞）。
void ble_srv_gatt_set_custom_cmd_callback(ble_srv_custom_cmd_cb_t cb);

// 主动向客户端发送自定义命令通知（需客户端已订阅 custom_cmd 特征）。
// 用于在命令回调之外异步推送数据，例如周期性状态上报。
void ble_srv_gatt_custom_cmd_notify(uint16_t conn_handle, const uint8_t *data, uint16_t data_len);

// 本地 preferred MTU。NimBLE 中 MTU exchange 由对端发起，
// 设备设置该值后客户端 exchange 时可争取到 512 字节 ATT payload。
#define BLE_SRV_PREFERRED_MTU       512

#ifdef __cplusplus
}
#endif

#endif
