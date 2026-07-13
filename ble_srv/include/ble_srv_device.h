#ifndef BLE_SRV_DEVICE_H
#define BLE_SRV_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_SRV_SVC_UUID              0xFFE0
#define BLE_SRV_CMD_CHAR_UUID         0xFFE1
#define BLE_SRV_INFO_CHAR_UUID        0xFFE2
#define BLE_SRV_MEMORY_CHAR_UUID      0xFFE3
#define BLE_SRV_CPU_CHAR_UUID         0xFFE4
#define BLE_SRV_FLASH_CHAR_UUID       0xFFE5
#define BLE_SRV_PARTITION_CHAR_UUID   0xFFE7
#define BLE_SRV_RESTART_CHAR_UUID     0xFFE6

typedef enum {
    BLE_SRV_CMD_GET_INFO = 0x01,
    BLE_SRV_CMD_GET_MEMORY = 0x02,
    BLE_SRV_CMD_GET_CPU = 0x03,
    BLE_SRV_CMD_GET_FLASH = 0x04,
    BLE_SRV_CMD_RESTART = 0x05,
} ble_srv_cmd_t;

typedef struct __attribute__((packed)) {
    uint32_t cpu_freq_mhz;
    uint32_t uptime_seconds;
    float temperature_celsius;
    uint8_t temp_sensor_supported;
    uint8_t reset_reason;
    uint8_t cpu_cores;
    char chip_name[32];
    char chip_model[16];
    char flash_size[16];
    char mac_address[18];
    char version[32];
} ble_srv_device_info_t;

typedef struct __attribute__((packed)) {
    uint32_t internal_total;
    uint32_t internal_free;
    uint32_t internal_min_free;
    uint32_t internal_largest;
    uint32_t psram_total;
    uint32_t psram_free;
    uint32_t psram_min_free;
    uint32_t psram_largest;
    uint32_t dma_free;
    uint32_t total_free;
    uint16_t task_count;
    uint16_t stack_hwm;
} ble_srv_memory_info_t;

typedef struct __attribute__((packed)) {
    uint32_t cpu_freq_mhz;
    uint32_t uptime_seconds;
    uint32_t features;
    uint16_t task_count;
    uint8_t cpu_cores;
    uint8_t cpu_usage;
    uint8_t chip_revision;
    char idf_version[24];
} ble_srv_cpu_info_t;

typedef struct __attribute__((packed)) {
    uint32_t flash_total;
    uint32_t flash_free;
    uint8_t flash_speed_mhz;
    uint8_t partition_count;
    char running_partition[16];
} ble_srv_flash_info_t;

typedef struct __attribute__((packed)) {
    uint32_t address;
    uint32_t size;
    uint8_t type;
    uint8_t subtype;
    char label[16];
} ble_srv_partition_info_t;

bool ble_srv_get_device_info(ble_srv_device_info_t *info);
bool ble_srv_get_memory_info(ble_srv_memory_info_t *info);
bool ble_srv_get_cpu_info(ble_srv_cpu_info_t *info);
bool ble_srv_get_flash_info(ble_srv_flash_info_t *info);
bool ble_srv_get_partition_info(uint8_t index, ble_srv_partition_info_t *info);
void ble_srv_device_deinit(void);

#ifdef __cplusplus
}
#endif

#endif
