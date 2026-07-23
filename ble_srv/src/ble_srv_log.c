#include "ble_srv_log.h"
#include "ble_srv_wifi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/unistd.h>
#include <dirent.h>
#include <time.h>
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/sdmmc_host.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

static const char *TAG = "BLE_SRV_LOG";

#define BLE_SRV_LOG_NVS_NAMESPACE "ble_log"
#define BLE_SRV_LOG_NVS_KEY_COUNT "log_count"

#define BLE_SRV_LOG_QUEUE_SIZE CONFIG_BLE_SRV_LOG_QUEUE_SIZE
#define BLE_SRV_LOG_LINE_SIZE CONFIG_BLE_SRV_LOG_LINE_SIZE
#define BLE_SRV_LOG_FLUSH_INTERVAL_MS CONFIG_BLE_SRV_LOG_FLUSH_INTERVAL_MS
#define BLE_SRV_LOG_FSYNC_EVERY_N_FLUSH CONFIG_BLE_SRV_LOG_FSYNC_EVERY_N_FLUSH

#ifndef CONFIG_BLE_SRV_NTP_TIMEZONE
#define CONFIG_BLE_SRV_NTP_TIMEZONE "CST-8"
#endif

static ble_srv_log_level_t s_log_level = BLE_SRV_LOG_LEVEL_INFO;
static ble_srv_log_storage_t s_storage = BLE_SRV_LOG_STORAGE_NONE;
static SemaphoreHandle_t s_log_lock = NULL;
static FILE *s_log_file = NULL;
static char s_log_file_path[128] = {0};
static uint32_t s_current_file_size = 0;
static bool s_initialized = false;
static int64_t s_boot_time_us = 0;
#ifdef CONFIG_BLE_SRV_LOG_SD_ENABLED
static sdmmc_card_t *s_sd_card = NULL;
#endif

static QueueHandle_t s_log_queue = NULL;
static uint8_t *s_log_queue_storage = NULL;
static StaticQueue_t s_log_queue_struct;
static esp_timer_handle_t s_flush_timer = NULL;
static uint32_t s_flush_count_since_fsync = 0;

// LOG_LOCK 返回 true 表示成功获取锁；超时返回 false，调用方必须检查并直接 return
#define LOG_LOCK()        (s_log_lock && xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(1000)) == pdTRUE)
#define LOG_UNLOCK()      do { if (s_log_lock) xSemaphoreGive(s_log_lock); } while(0)

static void ble_srv_log_flush_timer_cb(void *arg);

static const char *s_level_prefix[] = {
    "",
    "E",
    "W",
    "I",
    "D",
    "V",
};

static int64_t ble_srv_log_get_uptime_ms(void)
{
    return (esp_timer_get_time() - s_boot_time_us) / 1000;
}

bool ble_srv_log_time_is_valid(void)
{
    time_t now = time(NULL);
    struct tm tm_info;
    if (localtime_r(&now, &tm_info) == NULL) {
        return false;
    }
    return (tm_info.tm_year >= (2024 - 1900));
}

static int ble_srv_log_format_timestamp(char *buf, int buf_len)
{
    time_t now = time(NULL);
    struct tm tm_info;
    if (ble_srv_log_time_is_valid() && localtime_r(&now, &tm_info) != NULL) {
        return snprintf(buf, buf_len, "%04d-%02d-%02d %02d:%02d:%02d",
                        tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
                        tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
    } else {
        int64_t ms = ble_srv_log_get_uptime_ms();
        int64_t sec = ms / 1000;
        int ms_part = ms % 1000;
        return snprintf(buf, buf_len, "+%lld.%03d", (long long)sec, ms_part);
    }
}

static bool ble_srv_log_check_free_space(void)
{
    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    if (s_storage == BLE_SRV_LOG_STORAGE_LITTLEFS) {
        size_t total = 0, used = 0;
        if (esp_littlefs_info(BLE_SRV_LOG_LITTLEFS_PARTITION, &total, &used) == ESP_OK) {
            size_t free_space = total - used;
            return free_space >= BLE_SRV_LOG_MIN_FREE_SPACE;
        }
    }
    struct statvfs st;
    if (statvfs(base_path, &st) == 0) {
        size_t free_space = st.f_frsize * st.f_bavail;
        return free_space >= BLE_SRV_LOG_MIN_FREE_SPACE;
    }
    return true;
}

static bool ble_srv_log_is_valid_log_file(const char *filename)
{
    size_t len = strlen(filename);
    if (len != 10) return false;
    if (strncmp(filename + 6, ".log", 4) != 0) return false;
    for (int i = 0; i < 6; i++) {
        if (filename[i] < '0' || filename[i] > '9') return false;
    }
    return true;
}

static void ble_srv_log_cleanup_old_files(void)
{
    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    char log_dir[64];
    snprintf(log_dir, sizeof(log_dir), "%s%s", base_path, BLE_SRV_LOG_DIR);

    while (1) {
        if (ble_srv_log_check_free_space()) {
            break;
        }

        DIR *dir = opendir(log_dir);
        if (!dir) break;

        char oldest_file[256] = {0};
        time_t oldest_time = ~0ULL;
        struct dirent *entry;

        while ((entry = readdir(dir)) != NULL) {
            if (ble_srv_log_is_valid_log_file(entry->d_name)) {
                char filepath[384];
                snprintf(filepath, sizeof(filepath), "%s/%s", log_dir, entry->d_name);
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    if (st.st_mtime < oldest_time) {
                        oldest_time = st.st_mtime;
                        strncpy(oldest_file, filepath, sizeof(oldest_file) - 1);
                    }
                }
            }
        }
        closedir(dir);

        if (oldest_file[0]) {
            ESP_LOGI(TAG, "Deleting old log file to free space: %s", oldest_file);
            unlink(oldest_file);
        } else {
            break;
        }
    }

    while (1) {
        DIR *dir = opendir(log_dir);
        if (!dir) break;

        int file_count = 0;
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (ble_srv_log_is_valid_log_file(entry->d_name)) {
                file_count++;
            }
        }
        closedir(dir);

        if (file_count < BLE_SRV_LOG_MAX_FILES) break;

        dir = opendir(log_dir);
        if (!dir) break;

        char oldest_file[256] = {0};
        time_t oldest_time = ~0ULL;
        while ((entry = readdir(dir)) != NULL) {
            if (ble_srv_log_is_valid_log_file(entry->d_name)) {
                char filepath[384];
                snprintf(filepath, sizeof(filepath), "%s/%s", log_dir, entry->d_name);
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    if (st.st_mtime < oldest_time) {
                        oldest_time = st.st_mtime;
                        strncpy(oldest_file, filepath, sizeof(oldest_file) - 1);
                    }
                }
            }
        }
        closedir(dir);

        if (oldest_file[0]) {
            ESP_LOGI(TAG, "Deleting oldest log file (max count reached): %s", oldest_file);
            unlink(oldest_file);
        } else {
            break;
        }
    }
}

static bool ble_srv_log_mount_littlefs(void)
{
    ESP_LOGI(TAG, "Mounting LittleFS...");
    esp_vfs_littlefs_conf_t conf = {
        .base_path = BLE_SRV_LOG_LITTLEFS_PATH,
        .partition_label = BLE_SRV_LOG_LITTLEFS_PARTITION,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };
    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return false;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(BLE_SRV_LOG_LITTLEFS_PARTITION, &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LittleFS info: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "LittleFS mounted: total=%d, used=%d", (int)total, (int)used);

    /* LittleFS supports real directories - create log directory explicitly. */
    char log_dir[64];
    snprintf(log_dir, sizeof(log_dir), "%s%s", BLE_SRV_LOG_LITTLEFS_PATH, BLE_SRV_LOG_DIR);
    struct stat st;
    if (stat(log_dir, &st) != 0) {
        ESP_LOGI(TAG, "Creating log directory: %s", log_dir);
        if (mkdir(log_dir, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create log directory");
            return false;
        }
    }

    return true;
}

static bool ble_srv_log_mount_sd(void)
{
    ESP_LOGI(TAG, "Mounting SD card...");
#ifdef CONFIG_BLE_SRV_LOG_SD_ENABLED
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(BLE_SRV_LOG_SD_PATH, &host, &slot_config, &mount_config, &s_sd_card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));
        return false;
    }
    ESP_LOGI(TAG, "SD card mounted successfully");

    char log_dir[64];
    snprintf(log_dir, sizeof(log_dir), "%s%s", BLE_SRV_LOG_SD_PATH, BLE_SRV_LOG_DIR);
    struct stat st;
    if (stat(log_dir, &st) != 0) {
        ESP_LOGI(TAG, "Creating log directory: %s", log_dir);
        if (mkdir(log_dir, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create log directory");
            return false;
        }
    }

    return true;
#else
    ESP_LOGE(TAG, "SD card support not enabled");
    return false;
#endif
}

static void ble_srv_log_close_file(void)
{
    if (s_log_file) {
        fflush(s_log_file);
        fsync(fileno(s_log_file));
        fclose(s_log_file);
        s_log_file = NULL;
    }
    s_current_file_size = 0;
    s_log_file_path[0] = '\0';
}

static uint32_t ble_srv_log_get_next_file_number(void)
{
    uint32_t count = 0;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_SRV_LOG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return 0;
    }

    err = nvs_get_u32(handle, BLE_SRV_LOG_NVS_KEY_COUNT, &count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        count = 0;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get log count from NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }

    err = nvs_set_u32(handle, BLE_SRV_LOG_NVS_KEY_COUNT, count + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set log count in NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }

    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return 0;
    }

    nvs_close(handle);
    return count;
}

static bool ble_srv_log_open_new_file(void)
{
    ble_srv_log_close_file();
    ble_srv_log_cleanup_old_files();

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;

    char filename[64];
    uint32_t file_num = ble_srv_log_get_next_file_number();
    snprintf(filename, sizeof(filename), "%06lu.log", (unsigned long)file_num);

    snprintf(s_log_file_path, sizeof(s_log_file_path), "%s%s/%s", base_path, BLE_SRV_LOG_DIR, filename);

    s_log_file = fopen(s_log_file_path, "a");
    if (!s_log_file) {
        ESP_LOGE(TAG, "Failed to open log file: %s", s_log_file_path);
        return false;
    }

    s_current_file_size = 0;
    ESP_LOGI(TAG, "Log file opened: %s", s_log_file_path);
    return true;
}



bool ble_srv_log_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Log system already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing log system");

    setenv("TZ", CONFIG_BLE_SRV_NTP_TIMEZONE, 1);
    tzset();

    s_boot_time_us = esp_timer_get_time();

    s_log_lock = xSemaphoreCreateMutex();
    if (!s_log_lock) {
        ESP_LOGE(TAG, "Failed to create log lock");
        return false;
    }

    size_t queue_storage_size = (size_t)BLE_SRV_LOG_QUEUE_SIZE * BLE_SRV_LOG_LINE_SIZE;
    s_log_queue_storage = heap_caps_malloc(queue_storage_size, MALLOC_CAP_SPIRAM);
    if (!s_log_queue_storage) {
        ESP_LOGE(TAG, "Failed to allocate log queue storage (%d bytes) from PSRAM", (int)queue_storage_size);
        vSemaphoreDelete(s_log_lock);
        s_log_lock = NULL;
        return false;
    }
    memset(s_log_queue_storage, 0, queue_storage_size);

    s_log_queue = xQueueCreateStatic(BLE_SRV_LOG_QUEUE_SIZE, BLE_SRV_LOG_LINE_SIZE, s_log_queue_storage, &s_log_queue_struct);
    if (!s_log_queue) {
        ESP_LOGE(TAG, "Failed to create log queue");
        heap_caps_free(s_log_queue_storage);
        s_log_queue_storage = NULL;
        vSemaphoreDelete(s_log_lock);
        s_log_lock = NULL;
        return false;
    }

    if (ble_srv_log_mount_sd()) {
        s_storage = BLE_SRV_LOG_STORAGE_SD;
        ESP_LOGI(TAG, "Storage: SD card");
    } else if (ble_srv_log_mount_littlefs()) {
        s_storage = BLE_SRV_LOG_STORAGE_LITTLEFS;
        ESP_LOGI(TAG, "Storage: LittleFS");
    } else {
        s_storage = BLE_SRV_LOG_STORAGE_NONE;
        ESP_LOGW(TAG, "No storage available, log disabled");
    }

    if (s_storage != BLE_SRV_LOG_STORAGE_NONE) {
        if (!ble_srv_log_open_new_file()) {
            vSemaphoreDelete(s_log_lock);
            s_log_lock = NULL;
            vQueueDelete(s_log_queue);
            s_log_queue = NULL;
            heap_caps_free(s_log_queue_storage);
            s_log_queue_storage = NULL;
            return false;
        }

        esp_timer_create_args_t timer_args = {
            .callback = ble_srv_log_flush_timer_cb,
            .arg = NULL,
            .name = "log_flush_timer",
            .dispatch_method = ESP_TIMER_TASK,
            .skip_unhandled_events = true,
        };
        esp_err_t ret = esp_timer_create(&timer_args, &s_flush_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create flush timer: %s", esp_err_to_name(ret));
        } else {
            esp_timer_start_periodic(s_flush_timer, BLE_SRV_LOG_FLUSH_INTERVAL_MS * 1000ULL);
        }

        s_initialized = true;
        BLE_SRV_LOGI(TAG, "=== System started ===");
    } else {
        s_initialized = true;
    }

    ESP_LOGI(TAG, "Log system initialized");
    return true;
}

void ble_srv_log_deinit(void)
{
    if (!s_initialized) return;

    ESP_LOGI(TAG, "Deinitializing log system");

    if (s_flush_timer) {
        esp_timer_stop(s_flush_timer);
        esp_timer_delete(s_flush_timer);
        s_flush_timer = NULL;
    }

    ble_srv_log_flush_queue();

    if (LOG_LOCK()) {
        ble_srv_log_close_file();
        LOG_UNLOCK();
    }

    if (s_storage == BLE_SRV_LOG_STORAGE_LITTLEFS) {
        esp_vfs_littlefs_unregister(BLE_SRV_LOG_LITTLEFS_PARTITION);
    } else if (s_storage == BLE_SRV_LOG_STORAGE_SD) {
#ifdef CONFIG_BLE_SRV_LOG_SD_ENABLED
        esp_vfs_fat_sdcard_unmount(BLE_SRV_LOG_SD_PATH, s_sd_card);
        s_sd_card = NULL;
#endif
    }

    s_log_file_path[0] = '\0';

    if (s_log_lock) {
        vSemaphoreDelete(s_log_lock);
        s_log_lock = NULL;
    }

    if (s_log_queue) {
        vQueueDelete(s_log_queue);
        s_log_queue = NULL;
    }

    if (s_log_queue_storage) {
        heap_caps_free(s_log_queue_storage);
        s_log_queue_storage = NULL;
    }

    s_storage = BLE_SRV_LOG_STORAGE_NONE;
    s_initialized = false;
    ESP_LOGI(TAG, "Log system deinitialized");
}

void ble_srv_log_write(ble_srv_log_level_t level, const char *tag, const char *fmt, ...)
{
    if (level > s_log_level || !fmt) {
        return;
    }

    char body[BLE_SRV_LOG_LINE_SIZE];

    va_list ap;
    va_start(ap, fmt);
    int body_len = vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    if (body_len < 0) {
        body_len = 0;
    }
    if (body_len >= (int)sizeof(body)) {
        body_len = (int)sizeof(body) - 1;
    }
    body[body_len] = '\0';

#ifdef CONFIG_BLE_SRV_LOG_CONSOLE_ENABLED
    switch (level) {
    case BLE_SRV_LOG_LEVEL_ERROR:
        ESP_LOGE(tag, "%s", body);
        break;
    case BLE_SRV_LOG_LEVEL_WARN:
        ESP_LOGW(tag, "%s", body);
        break;
    case BLE_SRV_LOG_LEVEL_INFO:
        ESP_LOGI(tag, "%s", body);
        break;
    case BLE_SRV_LOG_LEVEL_DEBUG:
        ESP_LOGD(tag, "%s", body);
        break;
    case BLE_SRV_LOG_LEVEL_VERBOSE:
        ESP_LOGV(tag, "%s", body);
        break;
    default:
        break;
    }
#endif

    if (s_log_queue && s_storage != BLE_SRV_LOG_STORAGE_NONE) {
        char line[BLE_SRV_LOG_LINE_SIZE];
        char ts_buf[64];
        ble_srv_log_format_timestamp(ts_buf, sizeof(ts_buf));

        int prefix_len = snprintf(line, sizeof(line), "%s [%s] [%s] ",
                                  ts_buf, s_level_prefix[level], tag ? tag : "");
        if (prefix_len < 0) {
            prefix_len = 0;
        }
        if (prefix_len > (int)sizeof(line) - 2) {
            prefix_len = (int)sizeof(line) - 2;
        }

        int avail = (int)sizeof(line) - prefix_len - 2;
        int copy_len = body_len;
        if (copy_len > avail) {
            copy_len = avail;
        }

        memcpy(line + prefix_len, body, copy_len);
        line[prefix_len + copy_len] = '\n';
        line[prefix_len + copy_len + 1] = '\0';

        xQueueSend(s_log_queue, line, pdMS_TO_TICKS(10));
    }
}

void ble_srv_log_write_raw(const char *msg)
{
    if (!msg) {
        return;
    }

#ifdef CONFIG_BLE_SRV_LOG_CONSOLE_ENABLED
    printf("%s", msg);
#endif

    if (s_log_queue && s_storage != BLE_SRV_LOG_STORAGE_NONE) {
        char line[BLE_SRV_LOG_LINE_SIZE];
        int len = snprintf(line, sizeof(line), "%s", msg);
        if (len > 0 && len < (int)sizeof(line)) {
            xQueueSend(s_log_queue, line, pdMS_TO_TICKS(10));
        }
    }
}

static void ble_srv_log_flush_timer_cb(void *arg)
{
    ble_srv_log_flush_queue();
}

void ble_srv_log_flush_queue(void)
{
    if (!s_log_queue || !s_log_file || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        return;
    }

    char line[BLE_SRV_LOG_LINE_SIZE];
    int batch_count = 0;
    const int MAX_BATCH = 64;

    if (!LOG_LOCK()) {
        // 拿不到锁（HTTP handler 正在持有），本次 flush 跳过，下次定时器再触发
        return;
    }

    while (batch_count < MAX_BATCH && xQueueReceive(s_log_queue, line, 0) == pdTRUE) {
        size_t len = strlen(line);
        if (len > 0 && len < BLE_SRV_LOG_LINE_SIZE) {
            fwrite(line, 1, len, s_log_file);
            s_current_file_size += len;

            if (s_current_file_size >= BLE_SRV_LOG_MAX_FILE_SIZE) {
                ESP_LOGI(TAG, "Log file size limit reached, rotating...");
                // 文件轮转时必须 fsync 确保旧文件数据落盘
                if (s_log_file) {
                    fflush(s_log_file);
                    fsync(fileno(s_log_file));
                }
                s_flush_count_since_fsync = 0;
                ble_srv_log_close_file();
                ble_srv_log_open_new_file();
                if (!s_log_file) {
                    break;
                }
            }
        }
        batch_count++;
    }

    if (batch_count > 0) {
        // fflush 将 C 库缓冲写入 VFS 层（开销小）
        fflush(s_log_file);

        // fsync 强制物理写入 flash（开销大，按频率控制）
        bool should_fsync = false;
        if (BLE_SRV_LOG_FSYNC_EVERY_N_FLUSH > 0) {
            s_flush_count_since_fsync++;
            if (s_flush_count_since_fsync >= (uint32_t)BLE_SRV_LOG_FSYNC_EVERY_N_FLUSH) {
                should_fsync = true;
            }
        }

        if (should_fsync) {
            fsync(fileno(s_log_file));
            s_flush_count_since_fsync = 0;
        }
    }

    LOG_UNLOCK();
}

int ble_srv_log_get_file_list(ble_srv_log_file_info_t *files, int max_count)
{
    if (!files || max_count <= 0) {
        return -1;
    }

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        return 0;
    }

    if (!LOG_LOCK()) {
        return -1;
    }

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    char log_dir[64];
    snprintf(log_dir, sizeof(log_dir), "%s%s", base_path, BLE_SRV_LOG_DIR);

    DIR *dir = opendir(log_dir);
    if (!dir) {
        LOG_UNLOCK();
        return -1;
    }

    char *filepath = heap_caps_malloc(384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!filepath) filepath = heap_caps_malloc(384, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!filepath) {
        closedir(dir);
        LOG_UNLOCK();
        return -1;
    }

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL && count < max_count) {
        if (ble_srv_log_is_valid_log_file(entry->d_name)) {
            snprintf(filepath, 384, "%s/%s", log_dir, entry->d_name);

            struct stat st;
            if (stat(filepath, &st) == 0) {
                strncpy(files[count].name, entry->d_name, sizeof(files[count].name) - 1);
                files[count].name[sizeof(files[count].name) - 1] = '\0';
                files[count].size = st.st_size;
                files[count].mtime = st.st_mtime;
                count++;
            }
        }
    }

    heap_caps_free(filepath);
    closedir(dir);
    LOG_UNLOCK();

    return count;
}

int ble_srv_log_read_file(const char *filename, char *buffer, int max_len)
{
    if (!filename || !buffer || max_len <= 0) {
        return -1;
    }

    // 路径遍历防护：仅允许 "NNNNNN.log" 格式，拒绝任何含 '/' '\\' '..' 的输入。
    if (!ble_srv_log_is_valid_log_file(filename)) {
        ESP_LOGW(TAG, "Rejected invalid log filename: %s", filename);
        return -1;
    }

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        return -1;
    }

    if (!LOG_LOCK()) {
        return -1;
    }

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    char *filepath = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!filepath) filepath = heap_caps_malloc(256, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!filepath) {
        LOG_UNLOCK();
        return -1;
    }
    snprintf(filepath, 256, "%s%s/%s", base_path, BLE_SRV_LOG_DIR, filename);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open log file: %s (errno=%d)", filepath, errno);
        heap_caps_free(filepath);
        LOG_UNLOCK();
        return -1;
    }
    heap_caps_free(filepath);

    int read_len = fread(buffer, 1, max_len - 1, f);
    buffer[read_len] = '\0';

    fclose(f);
    LOG_UNLOCK();

    ESP_LOGI(TAG, "Read log file: %s (%d bytes)", filename, read_len);
    return read_len;
}

int ble_srv_log_read_file_lines(const char *filename, int max_lines, char *buffer, int buf_len)
{
    if (!filename || !buffer || max_lines <= 0 || buf_len <= 0) {
        return -1;
    }

    // 路径遍历防护：仅允许 "NNNNNN.log" 格式。
    if (!ble_srv_log_is_valid_log_file(filename)) {
        ESP_LOGW(TAG, "Rejected invalid log filename: %s", filename);
        return -1;
    }

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        return -1;
    }

    if (!LOG_LOCK()) {
        return -1;
    }

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s/%s", base_path, BLE_SRV_LOG_DIR, filename);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        LOG_UNLOCK();
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size < 0) {
        fclose(f);
        LOG_UNLOCK();
        return -1;
    }
    fseek(f, 0, SEEK_SET);

    char *file_buf = (char *)malloc(file_size + 1);
    if (!file_buf) {
        fclose(f);
        LOG_UNLOCK();
        return -1;
    }

    int read_len = fread(file_buf, 1, file_size, f);
    file_buf[read_len] = '\0';
    fclose(f);

    int line_count = 0;
    char *line_start = file_buf + read_len;
    for (long i = read_len - 1; i >= 0 && line_count < max_lines; i--) {
        if (file_buf[i] == '\n' || i == 0) {
            line_count++;
            line_start = file_buf + i;
            if (i == 0 && file_buf[i] != '\n') {
                line_start = file_buf;
            }
        }
    }

    int copy_len = file_buf + read_len - line_start;
    if (copy_len > buf_len - 1) {
        line_start += (copy_len - (buf_len - 1));
        copy_len = buf_len - 1;
    }
    memcpy(buffer, line_start, copy_len);
    buffer[copy_len] = '\0';

    free(file_buf);
    LOG_UNLOCK();

    return copy_len;
}

bool ble_srv_log_delete_file(const char *filename)
{
    if (!filename) {
        return false;
    }

    // 路径遍历防护：仅允许 "NNNNNN.log" 格式。
    if (!ble_srv_log_is_valid_log_file(filename)) {
        ESP_LOGW(TAG, "Rejected invalid log filename: %s", filename);
        return false;
    }

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        return false;
    }

    if (!LOG_LOCK()) {
        return false;
    }

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "%s%s/%s", base_path, BLE_SRV_LOG_DIR, filename);

    bool result = (unlink(filepath) == 0);

    LOG_UNLOCK();

    return result;
}

bool ble_srv_log_get_storage_info(ble_srv_log_storage_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        return false;
    }

    info->storage_type = (uint8_t)s_storage;
    info->log_level = (uint8_t)s_log_level;

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;

    if (s_storage == BLE_SRV_LOG_STORAGE_LITTLEFS) {
        size_t total = 0, used = 0;
        if (esp_littlefs_info(BLE_SRV_LOG_LITTLEFS_PARTITION, &total, &used) == ESP_OK) {
            info->total_size = (uint32_t)total;
            info->used_size = (uint32_t)used;
            info->free_size = (uint32_t)(total - used);
        }
    } else {
        struct statvfs vfs_buf;
        if (statvfs(base_path, &vfs_buf) == 0) {
            info->total_size = (uint32_t)(vfs_buf.f_frsize * vfs_buf.f_blocks);
            info->free_size = (uint32_t)(vfs_buf.f_frsize * vfs_buf.f_bavail);
            info->used_size = info->total_size - info->free_size;
        }
    }

    char log_dir[256];
    snprintf(log_dir, sizeof(log_dir), "%s%s", base_path, BLE_SRV_LOG_DIR);
    DIR *dir = opendir(log_dir);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                info->file_count++;
            }
        }
        closedir(dir);
    }

    return true;
}

void ble_srv_log_set_level(ble_srv_log_level_t level)
{
    s_log_level = level;
}

ble_srv_log_level_t ble_srv_log_get_level(void)
{
    return s_log_level;
}

ble_srv_log_storage_t ble_srv_log_get_storage(void)
{
    return s_storage;
}

// ==================== HTTP Server ====================

static httpd_handle_t s_http_server = NULL;

static esp_err_t log_http_index_handler(httpd_req_t *req)
{
    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        const char *msg = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Log Files</title></head><body style=\"font-family:sans-serif;padding:20px;\"><h2>Log system not initialized</h2></body></html>";
        httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    if (!LOG_LOCK()) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    const char *storage_name = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? "SD Card" : "LittleFS";
    char log_dir[64];
    snprintf(log_dir, sizeof(log_dir), "%s%s", base_path, BLE_SRV_LOG_DIR);

    DIR *dir = opendir(log_dir);
    if (!dir) {
        LOG_UNLOCK();
        const char *msg = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Log Files</title></head><body style=\"font-family:sans-serif;padding:20px;\"><h2>No log directory</h2></body></html>";
        httpd_resp_send(req, msg, HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    size_t total_space = 0, used_space = 0, free_space = 0;
    if (s_storage == BLE_SRV_LOG_STORAGE_LITTLEFS) {
        size_t total = 0, used = 0;
        if (esp_littlefs_info(BLE_SRV_LOG_LITTLEFS_PARTITION, &total, &used) == ESP_OK) {
            total_space = total;
            used_space = used;
            free_space = total - used;
        }
    } else {
        struct statvfs st;
        if (statvfs(base_path, &st) == 0) {
            total_space = st.f_frsize * st.f_blocks;
            free_space = st.f_frsize * st.f_bavail;
            used_space = total_space - free_space;
        }
    }

    int file_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (ble_srv_log_is_valid_log_file(entry->d_name)) {
            file_count++;
        }
    }
    rewinddir(dir);

    char *html = heap_caps_malloc(16384, MALLOC_CAP_SPIRAM);
    if (!html) {
        closedir(dir);
        LOG_UNLOCK();
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char total_str[32], used_str[32], free_str[32];
    if (total_space >= 1024 * 1024) {
        snprintf(total_str, sizeof(total_str), "%.2f MB", (float)total_space / (1024 * 1024));
        snprintf(used_str, sizeof(used_str), "%.2f MB", (float)used_space / (1024 * 1024));
        snprintf(free_str, sizeof(free_str), "%.2f MB", (float)free_space / (1024 * 1024));
    } else if (total_space >= 1024) {
        snprintf(total_str, sizeof(total_str), "%.2f KB", (float)total_space / 1024);
        snprintf(used_str, sizeof(used_str), "%.2f KB", (float)used_space / 1024);
        snprintf(free_str, sizeof(free_str), "%.2f KB", (float)free_space / 1024);
    } else {
        snprintf(total_str, sizeof(total_str), "%u B", (unsigned)total_space);
        snprintf(used_str, sizeof(used_str), "%u B", (unsigned)used_space);
        snprintf(free_str, sizeof(free_str), "%u B", (unsigned)free_space);
    }

    int used_percent = total_space > 0 ? (int)(used_space * 100 / total_space) : 0;

    int len = snprintf(html, 16384,
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no\">"
        "<title>Log Files</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0;}"
        "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,'Helvetica Neue',Arial,sans-serif;background:#f5f5f7;color:#1d1d1f;padding:16px;}"
        ".container{max-width:800px;margin:0 auto;}"
        "h1{font-size:24px;margin-bottom:16px;text-align:center;}"
        ".storage-card{background:#fff;border-radius:12px;padding:16px;margin-bottom:16px;box-shadow:0 2px 8px rgba(0,0,0,0.06);}"
        ".storage-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;}"
        ".storage-title{font-size:16px;font-weight:600;color:#1d1d1f;}"
        ".storage-badge{background:#e8f0fe;color:#1a73e8;padding:4px 10px;border-radius:20px;font-size:12px;font-weight:500;}"
        ".stats-grid{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin-bottom:12px;}"
        ".stat-item{text-align:center;padding:8px;background:#f8f9fa;border-radius:8px;}"
        ".stat-value{font-size:18px;font-weight:700;color:#1d1d1f;}"
        ".stat-label{font-size:11px;color:#86868b;margin-top:2px;}"
        ".progress-bar{width:100%%;height:8px;background:#e8e8ed;border-radius:4px;overflow:hidden;}"
        ".progress-fill{height:100%%;background:linear-gradient(90deg,#007aff,#34c759);border-radius:4px;transition:width 0.3s;}"
        ".progress-info{display:flex;justify-content:space-between;font-size:12px;color:#86868b;margin-top:6px;}"
        ".file-list{background:#fff;border-radius:12px;overflow:hidden;box-shadow:0 2px 8px rgba(0,0,0,0.06);}"
        ".file-list-header{padding:14px 16px;border-bottom:1px solid #e8e8ed;font-size:14px;font-weight:600;color:#1d1d1f;display:flex;justify-content:space-between;align-items:center;}"
        ".file-count{color:#86868b;font-weight:400;font-size:13px;}"
        ".file-item{display:flex;justify-content:space-between;align-items:center;padding:14px 16px;border-bottom:1px solid #f2f2f7;text-decoration:none;color:#1d1d1f;transition:background 0.15s;}"
        ".file-item:last-child{border-bottom:none;}"
        ".file-item:hover{background:#f8f9fa;}"
        ".file-item:active{background:#f0f0f0;}"
        ".file-info{flex:1;min-width:0;}"
        ".file-name{font-size:15px;font-weight:500;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}"
        ".file-time{font-size:12px;color:#86868b;margin-top:4px;}"
        ".file-size{font-size:13px;color:#86868b;font-weight:500;margin-left:12px;flex-shrink:0;}"
        ".empty-state{padding:40px 20px;text-align:center;color:#86868b;}"
        ".empty-icon{font-size:48px;margin-bottom:12px;}"
        ".empty-text{font-size:14px;}"
        "@media (max-width:480px){"
        "body{padding:12px;}"
        "h1{font-size:20px;}"
        ".storage-card{padding:12px;}"
        ".stat-value{font-size:16px;}"
        ".stat-label{font-size:10px;}"
        ".file-item{padding:12px;}"
        ".file-name{font-size:14px;}"
        ".file-time{font-size:11px;}"
        "}"
        "</style></head><body>"
        "<div class=\"container\">"
        "<h1>📋 日志文件</h1>"
        "<div class=\"storage-card\">"
        "<div class=\"storage-header\">"
        "<span class=\"storage-title\">存储信息</span>"
        "<span class=\"storage-badge\">%s</span>"
        "</div>"
        "<div class=\"stats-grid\">"
        "<div class=\"stat-item\">"
        "<div class=\"stat-value\">%s</div>"
        "<div class=\"stat-label\">总容量</div>"
        "</div>"
        "<div class=\"stat-item\">"
        "<div class=\"stat-value\">%s</div>"
        "<div class=\"stat-label\">已使用</div>"
        "</div>"
        "<div class=\"stat-item\">"
        "<div class=\"stat-value\">%s</div>"
        "<div class=\"stat-label\">可用</div>"
        "</div>"
        "</div>"
        "<div class=\"progress-bar\">"
        "<div class=\"progress-fill\" style=\"width:%d%%\"></div>"
        "</div>"
        "<div class=\"progress-info\">"
        "<span>已使用 %d%%</span>"
        "<span>%d 个文件</span>"
        "</div>"
        "</div>"
        "<div class=\"file-list\">"
        "<div class=\"file-list-header\">"
        "<span>文件列表</span>"
        "<span class=\"file-count\">共 %d 个</span>"
        "</div>",
        storage_name, total_str, used_str, free_str,
        used_percent, used_percent, file_count, file_count);

    if (file_count == 0) {
        int remaining = 16384 - len;
        if (remaining > 128) {
            len += snprintf(html + len, remaining,
                "<div class=\"empty-state\">"
                "<div class=\"empty-icon\">📭</div>"
                "<div class=\"empty-text\">暂无日志文件</div>"
                "</div>");
        }
    } else {
        #define MAX_LIST_FILES 200
        typedef struct {
            char name[BLE_SRV_LOG_FILENAME_LEN];
            uint32_t size;
            time_t mtime;
        } log_file_entry_t;

        log_file_entry_t *files = heap_caps_malloc(sizeof(log_file_entry_t) * MAX_LIST_FILES, MALLOC_CAP_SPIRAM);
        if (!files) {
            closedir(dir);
            LOG_UNLOCK();
            heap_caps_free(html);
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        int count = 0;
        while ((entry = readdir(dir)) != NULL && count < MAX_LIST_FILES) {
            if (ble_srv_log_is_valid_log_file(entry->d_name)) {
                char filepath[384];
                snprintf(filepath, sizeof(filepath), "%s/%s", log_dir, entry->d_name);
                struct stat st;
                if (stat(filepath, &st) == 0) {
                    strncpy(files[count].name, entry->d_name, BLE_SRV_LOG_FILENAME_LEN - 1);
                    files[count].name[BLE_SRV_LOG_FILENAME_LEN - 1] = '\0';
                    files[count].size = st.st_size;
                    files[count].mtime = st.st_mtime;
                    count++;
                }
            }
        }

        for (int i = 1; i < count; i++) {
            log_file_entry_t key = files[i];
            int j = i - 1;
            while (j >= 0 && strcmp(files[j].name, key.name) < 0) {
                files[j + 1] = files[j];
                j--;
            }
            files[j + 1] = key;
        }

        for (int i = 0; i < count; i++) {
            char size_str[32];
            if (files[i].size >= 1024 * 1024) {
                snprintf(size_str, sizeof(size_str), "%.2f MB", (float)files[i].size / (1024 * 1024));
            } else if (files[i].size >= 1024) {
                snprintf(size_str, sizeof(size_str), "%.2f KB", (float)files[i].size / 1024);
            } else {
                snprintf(size_str, sizeof(size_str), "%u B", (unsigned)files[i].size);
            }

            char time_str[64];
            struct tm tm_info;
            if (localtime_r(&files[i].mtime, &tm_info) != NULL) {
                snprintf(time_str, sizeof(time_str), "%04d-%02d-%02d %02d:%02d:%02d",
                    tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
                    tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec);
            } else {
                snprintf(time_str, sizeof(time_str), "Unknown");
            }

            int remaining = 16384 - len;
            if (remaining > 384) {
                len += snprintf(html + len, remaining,
                    "<a href=\"/logs/%s\" class=\"file-item\">"
                    "<div class=\"file-info\">"
                    "<div class=\"file-name\">📄 %s</div>"
                    "<div class=\"file-time\">🕐 %s</div>"
                    "</div>"
                    "<div class=\"file-size\">%s</div>"
                    "</a>",
                    files[i].name, files[i].name, time_str, size_str);
            }
        }

        heap_caps_free(files);
    }
    closedir(dir);

    int remaining = 16384 - len;
    if (remaining > 32) {
        len += snprintf(html + len, remaining,
            "</div></div></body></html>");
    }

    LOG_UNLOCK();

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, html, len);
    heap_caps_free(html);
    return ESP_OK;
}

static esp_err_t log_http_favicon_handler(httpd_req_t *req)
{
    const uint8_t favicon_png[] = {
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
        0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
        0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
        0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
        0x42, 0x60, 0x82,
    };
    httpd_resp_set_type(req, "image/png");
    httpd_resp_send(req, (const char *)favicon_png, sizeof(favicon_png));
    return ESP_OK;
}

static esp_err_t log_http_file_handler(httpd_req_t *req)
{
    const char *uri = req->uri;
    if (strncmp(uri, "/logs/", 6) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    const char *filename = uri + 6;
    if (strlen(filename) == 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // 路径遍历防护：HTTP 入口最易受攻击，严格校验 "NNNNNN.log" 格式。
    if (!ble_srv_log_is_valid_log_file(filename)) {
        ESP_LOGW(TAG, "HTTP rejected invalid log filename: %s", filename);
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // 注意：此处不持 s_log_lock。读的是用户指定的历史文件，
    // 与 flush_queue 写入的活跃文件 s_log_file 无关；VFS FAT/LittleFS 自身线程安全。
    const char *base_path = (s_storage == BLE_SRV_LOG_STORAGE_SD) ? BLE_SRV_LOG_SD_PATH : BLE_SRV_LOG_LITTLEFS_PATH;
    char filepath[384];
    snprintf(filepath, sizeof(filepath), "%s%s/%s", base_path, BLE_SRV_LOG_DIR, filename);

    FILE *f = fopen(filepath, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/plain");

    char *buffer = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!buffer) {
        fclose(f);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int read_len;
    while ((read_len = fread(buffer, 1, 4096, f)) > 0) {
        httpd_resp_send_chunk(req, buffer, read_len);
    }
    httpd_resp_send_chunk(req, NULL, 0);

    fclose(f);
    heap_caps_free(buffer);

    return ESP_OK;
}

bool ble_srv_log_http_start(void)
{
    if (s_http_server) {
        return true;
    }

    if (!s_initialized || s_storage == BLE_SRV_LOG_STORAGE_NONE) {
        ESP_LOGE(TAG, "Log system not initialized, cannot start HTTP server");
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = BLE_SRV_LOG_HTTP_PORT;
    config.max_uri_handlers = 8;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t ret = httpd_start(&s_http_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        s_http_server = NULL;
        return false;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = log_http_index_handler,
    };
    httpd_uri_t file_uri = {
        .uri = "/logs/*",
        .method = HTTP_GET,
        .handler = log_http_file_handler,
    };
    httpd_uri_t favicon_uri = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = log_http_favicon_handler,
    };

    httpd_register_uri_handler(s_http_server, &index_uri);
    httpd_register_uri_handler(s_http_server, &file_uri);
    httpd_register_uri_handler(s_http_server, &favicon_uri);

    ESP_LOGI(TAG, "HTTP server started on port %d", BLE_SRV_LOG_HTTP_PORT);
    return true;
}

void ble_srv_log_http_stop(void)
{
    if (s_http_server) {
        httpd_stop(s_http_server);
        s_http_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

bool ble_srv_log_http_is_running(void)
{
    return s_http_server != NULL;
}

bool ble_srv_log_format_littlefs(void)
{
    if (!s_initialized || s_storage != BLE_SRV_LOG_STORAGE_LITTLEFS) {
        return false;
    }

    if (!LOG_LOCK()) {
        return false;
    }

    if (s_log_file) {
        fflush(s_log_file);
        fclose(s_log_file);
        s_log_file = NULL;
    }

    esp_vfs_littlefs_unregister(BLE_SRV_LOG_LITTLEFS_PARTITION);

    esp_err_t ret = esp_littlefs_format(BLE_SRV_LOG_LITTLEFS_PARTITION);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to format LittleFS: %s", esp_err_to_name(ret));
        LOG_UNLOCK();
        return false;
    }

    esp_vfs_littlefs_conf_t conf = {
        .base_path = BLE_SRV_LOG_LITTLEFS_PATH,
        .partition_label = BLE_SRV_LOG_LITTLEFS_PARTITION,
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to re-mount LittleFS: %s", esp_err_to_name(ret));
        LOG_UNLOCK();
        return false;
    }

    char log_dir[64];
    snprintf(log_dir, sizeof(log_dir), "%s%s", BLE_SRV_LOG_LITTLEFS_PATH, BLE_SRV_LOG_DIR);
    mkdir(log_dir, 0755);

    /* 格式化后重置NVS日志文件计数器，使文件名从 000001.log 重新开始 */
    {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(BLE_SRV_LOG_NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            nvs_set_u32(handle, BLE_SRV_LOG_NVS_KEY_COUNT, 0);
            nvs_commit(handle);
            nvs_close(handle);
        }
    }

    ble_srv_log_open_new_file();

    LOG_UNLOCK();

    ESP_LOGI(TAG, "LittleFS formatted successfully");
    return true;
}

