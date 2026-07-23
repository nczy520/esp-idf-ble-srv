#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "ble_srv.h"
#include "ble_srv_log.h"

static const char *TAG = "APP_MAIN";

// 自定义命令处理回调：客户端通过 BLE 自定义命令特征(0xFFEA)写入文本命令，
// 此函数被 ble_srv 任务线程调用。响应数据通过 NOTIFY 回传给客户端。
// 支持的命令（ASCII 文本，大小写敏感）：
//   help              - 列出可用命令
//   time              - 返回当前日期时间
//   uptime            - 返回设备运行时长（秒）
//   heap              - 返回可用堆内存信息
//   version           - 返回固件版本号
//   echo <text>       - 原样回显文本
//   reboot            - 重启设备
static int app_custom_cmd_handler(uint16_t conn_handle, const uint8_t *data, uint16_t data_len,
                                   uint8_t *resp_buf, size_t resp_buf_len, uint16_t *resp_len)
{
    (void)conn_handle;
    if (data_len == 0 || resp_buf_len == 0) {
        if (resp_len) *resp_len = 0;
        return -1;
    }

    // 将输入拷贝为以 '\0' 结尾的字符串便于解析（长度截断到 resp_buf 容量-1 内）
    char cmd[128];
    uint16_t copy_len = data_len < sizeof(cmd) - 1 ? data_len : sizeof(cmd) - 1;
    memcpy(cmd, data, copy_len);
    cmd[copy_len] = '\0';

    // 去除尾部空白字符（\r\n 等），便于匹配命令
    while (copy_len > 0 && (cmd[copy_len - 1] == '\r' || cmd[copy_len - 1] == '\n' ||
                             cmd[copy_len - 1] == ' ' || cmd[copy_len - 1] == '\t')) {
        cmd[--copy_len] = '\0';
    }

    if (copy_len == 0) {
        int n = snprintf((char *)resp_buf, resp_buf_len, "empty command");
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // help：列出所有可用命令
    if (strcmp(cmd, "help") == 0) {
        int n = snprintf((char *)resp_buf, resp_buf_len,
                          "commands:\n"
                          "  help        - list commands\n"
                          "  time        - current date/time\n"
                          "  uptime      - uptime in seconds\n"
                          "  heap        - free heap info\n"
                          "  version     - firmware version\n"
                          "  echo <text> - echo text back\n"
                          "  reboot      - restart device");
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // time：返回当前日期时间
    if (strcmp(cmd, "time") == 0) {
        time_t now;
        time(&now);
        struct tm tm_local;
        localtime_r(&now, &tm_local);
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_local);
        int n = snprintf((char *)resp_buf, resp_buf_len, "time: %s", time_str);
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // uptime：返回设备运行时长（秒）
    if (strcmp(cmd, "uptime") == 0) {
        uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);
        int n = snprintf((char *)resp_buf, resp_buf_len, "uptime: %lus", (unsigned long)uptime);
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // heap：返回堆内存信息
    if (strcmp(cmd, "heap") == 0) {
        uint32_t free = esp_get_free_heap_size();
        uint32_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        int n = snprintf((char *)resp_buf, resp_buf_len,
                          "heap free: %luB\ninternal free: %luB\npsram free: %luB",
                          (unsigned long)free, (unsigned long)internal_free,
                          (unsigned long)psram_free);
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // version：返回固件版本号
    if (strcmp(cmd, "version") == 0) {
        const esp_app_desc_t *desc = esp_app_get_description();
        int n = snprintf((char *)resp_buf, resp_buf_len, "version: %s",
                          desc ? desc->version : "unknown");
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // reboot：重启设备
    if (strcmp(cmd, "reboot") == 0) {
        int n = snprintf((char *)resp_buf, resp_buf_len, "rebooting...");
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        BLE_SRV_LOGI(TAG, "reboot requested via custom command");
        // 延迟 500ms 重启，确保 notify 响应能先发送给客户端
        ble_srv_schedule_restart(500);
        return 0;
    }

    // echo <text>：原样回显
    if (strncmp(cmd, "echo ", 5) == 0) {
        const char *text = cmd + 5;
        int n = snprintf((char *)resp_buf, resp_buf_len, "echo: %s", text);
        if (n < 0) n = 0;
        if (resp_len) *resp_len = (uint16_t)n;
        return 0;
    }

    // 未知命令
    int n = snprintf((char *)resp_buf, resp_buf_len, "unknown command: %s (type 'help')", cmd);
    if (n < 0) n = 0;
    if (resp_len) *resp_len = (uint16_t)n;
    return 0;
}

void app_main(void)
{
    BLE_SRV_LOGI(TAG, "Initializing BLE Service Example");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!ble_srv_log_init()) {
        BLE_SRV_LOGE(TAG, "Log system initialization failed");
        return;
    }
    BLE_SRV_LOGI(TAG, "Log system initialized");

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    if (!ble_srv_wifi_provisioner_init()) {
        BLE_SRV_LOGE(TAG, "WiFi provisioner initialization failed");
        return;
    }
#endif

    if (!ble_srv_init()) {
        BLE_SRV_LOGE(TAG, "BLE Service initialization failed");
        return;
    }

    // 注册自定义命令处理回调，开放项目侧功能给 BLE 客户端
    ble_srv_gatt_set_custom_cmd_callback(app_custom_cmd_handler);
    BLE_SRV_LOGI(TAG, "Custom command handler registered (try: help/time/uptime/heap/version/echo/reboot)");

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    ble_srv_wifi_auto_connect();
#endif

    BLE_SRV_LOGI(TAG, "BLE Service Example started successfully");

#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
    BLE_SRV_LOGI(TAG, "WiFi provisioner: started");
#endif

    while (1) {
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
        static bool last_wifi_connected = false;
        bool wifi_connected = ble_srv_wifi_is_connected();
        if (wifi_connected && !last_wifi_connected) {
            BLE_SRV_LOGI(TAG, "WiFi connected!");
            last_wifi_connected = true;
        } else if (!wifi_connected && last_wifi_connected) {
            BLE_SRV_LOGI(TAG, "WiFi disconnected!");
            last_wifi_connected = false;
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
