#ifndef BLE_SRV_TEMP_SENSOR_H
#define BLE_SRV_TEMP_SENSOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化温度传感器
 * 
 * @return true 初始化成功
 * @return false 初始化失败或芯片不支持
 */
bool ble_srv_temp_sensor_init(void);

/**
 * @brief 读取温度传感器温度值
 * 
 * @param out_celsius 输出温度值（摄氏度）
 * @return true 读取成功
 * @return false 读取失败
 */
bool ble_srv_temp_sensor_read(float *out_celsius);

/**
 * @brief 反初始化温度传感器
 * 
 */
void ble_srv_temp_sensor_deinit(void);

/**
 * @brief 检查当前芯片是否支持温度传感器
 * 
 * @return true 支持
 * @return false 不支持
 */
bool ble_srv_temp_sensor_is_supported(void);

#ifdef __cplusplus
}
#endif

#endif // BLE_SRV_TEMP_SENSOR_H
