#ifndef BLE_SRV_LOG_H
#define BLE_SRV_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_BLE_SRV_LOG_DIR
#define CONFIG_BLE_SRV_LOG_DIR "/log"
#endif
#ifndef CONFIG_BLE_SRV_LOG_LITTLEFS_PATH
#define CONFIG_BLE_SRV_LOG_LITTLEFS_PATH "/littlefs"
#endif
#ifndef CONFIG_BLE_SRV_LOG_LITTLEFS_PARTITION
#define CONFIG_BLE_SRV_LOG_LITTLEFS_PARTITION "littlefs"
#endif
#ifndef CONFIG_BLE_SRV_LOG_SD_PATH
#define CONFIG_BLE_SRV_LOG_SD_PATH "/sdcard"
#endif

#define BLE_SRV_LOG_DIR                  CONFIG_BLE_SRV_LOG_DIR
#define BLE_SRV_LOG_LITTLEFS_PATH        CONFIG_BLE_SRV_LOG_LITTLEFS_PATH
#define BLE_SRV_LOG_LITTLEFS_PARTITION   CONFIG_BLE_SRV_LOG_LITTLEFS_PARTITION
#define BLE_SRV_LOG_SD_PATH              CONFIG_BLE_SRV_LOG_SD_PATH

#ifndef CONFIG_BLE_SRV_LOG_MAX_FILES
#define CONFIG_BLE_SRV_LOG_MAX_FILES 50
#endif

#ifndef CONFIG_BLE_SRV_LOG_MAX_FILE_SIZE
#define CONFIG_BLE_SRV_LOG_MAX_FILE_SIZE 512
#endif

#ifndef CONFIG_BLE_SRV_LOG_MIN_FREE_SPACE
#define CONFIG_BLE_SRV_LOG_MIN_FREE_SPACE 256
#endif

#ifndef CONFIG_BLE_SRV_LOG_QUEUE_SIZE
#define CONFIG_BLE_SRV_LOG_QUEUE_SIZE 1024
#endif

#ifndef CONFIG_BLE_SRV_LOG_LINE_SIZE
#define CONFIG_BLE_SRV_LOG_LINE_SIZE 512
#endif

#ifndef CONFIG_BLE_SRV_LOG_FLUSH_INTERVAL_MS
#define CONFIG_BLE_SRV_LOG_FLUSH_INTERVAL_MS 1000
#endif

#define BLE_SRV_LOG_MAX_FILES        CONFIG_BLE_SRV_LOG_MAX_FILES
#define BLE_SRV_LOG_MAX_FILE_SIZE    (CONFIG_BLE_SRV_LOG_MAX_FILE_SIZE * 1024)
#define BLE_SRV_LOG_MIN_FREE_SPACE   (CONFIG_BLE_SRV_LOG_MIN_FREE_SPACE * 1024)
#define BLE_SRV_LOG_FILE_LIST_MAX    20
#define BLE_SRV_LOG_FILENAME_LEN     16

#ifndef CONFIG_BLE_SRV_LOG_HTTP_PORT
#define CONFIG_BLE_SRV_LOG_HTTP_PORT 80
#endif
#define BLE_SRV_LOG_HTTP_PORT        CONFIG_BLE_SRV_LOG_HTTP_PORT

typedef enum {
    BLE_SRV_LOG_LEVEL_NONE = 0,
    BLE_SRV_LOG_LEVEL_ERROR,
    BLE_SRV_LOG_LEVEL_WARN,
    BLE_SRV_LOG_LEVEL_INFO,
    BLE_SRV_LOG_LEVEL_DEBUG,
    BLE_SRV_LOG_LEVEL_VERBOSE,
} ble_srv_log_level_t;

typedef enum {
    BLE_SRV_LOG_STORAGE_NONE = 0,
    BLE_SRV_LOG_STORAGE_LITTLEFS,
    BLE_SRV_LOG_STORAGE_SD,
} ble_srv_log_storage_t;

typedef struct __attribute__((packed)) {
    char name[BLE_SRV_LOG_FILENAME_LEN];
    uint32_t size;
    uint32_t mtime;
} ble_srv_log_file_info_t;

typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t used_size;
    uint32_t free_size;
    uint32_t file_count;
    uint8_t storage_type;
    uint8_t log_level;
} ble_srv_log_storage_info_t;

bool ble_srv_log_init(void);
void ble_srv_log_deinit(void);
void ble_srv_log_write(ble_srv_log_level_t level, const char *tag, const char *fmt, ...);
void ble_srv_log_write_raw(const char *msg);
void ble_srv_log_flush_queue(void);
int ble_srv_log_get_file_list(ble_srv_log_file_info_t *files, int max_count);
int ble_srv_log_read_file(const char *filename, char *buffer, int max_len);
int ble_srv_log_read_file_lines(const char *filename, int max_lines, char *buffer, int buf_len);
bool ble_srv_log_delete_file(const char *filename);
void ble_srv_log_set_level(ble_srv_log_level_t level);
ble_srv_log_level_t ble_srv_log_get_level(void);
ble_srv_log_storage_t ble_srv_log_get_storage(void);
bool ble_srv_log_time_is_valid(void);
bool ble_srv_log_get_storage_info(ble_srv_log_storage_info_t *info);

typedef enum {
    BLE_SRV_LOG_HTTP_CMD_STOP = 0,
    BLE_SRV_LOG_HTTP_CMD_START = 1,
    BLE_SRV_LOG_HTTP_CMD_STATUS = 2,
    BLE_SRV_LOG_HTTP_CMD_WRITE_LOG = 3,
    BLE_SRV_LOG_HTTP_CMD_FORMAT_LITTLEFS = 5,
    BLE_SRV_LOG_HTTP_CMD_SET_LEVEL = 6,
} ble_srv_log_http_cmd_t;

bool ble_srv_log_http_start(void);
void ble_srv_log_http_stop(void);
bool ble_srv_log_http_is_running(void);
bool ble_srv_log_format_littlefs(void);

#define BLE_SRV_LOGE(tag, fmt, ...) ble_srv_log_write(BLE_SRV_LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define BLE_SRV_LOGW(tag, fmt, ...) ble_srv_log_write(BLE_SRV_LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define BLE_SRV_LOGI(tag, fmt, ...) ble_srv_log_write(BLE_SRV_LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define BLE_SRV_LOGD(tag, fmt, ...) ble_srv_log_write(BLE_SRV_LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define BLE_SRV_LOGV(tag, fmt, ...) ble_srv_log_write(BLE_SRV_LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif
