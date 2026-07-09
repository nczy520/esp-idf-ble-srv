#include "ble_srv_device.h"
#include "ble_srv_temp_sensor.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "BLE_SRV_DEVICE";

bool ble_srv_get_device_info(ble_srv_device_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    switch (chip_info.model) {
    case CHIP_ESP32:
        strncpy(info->chip_name, "ESP32", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "ESP32", sizeof(info->chip_model) - 1);
        break;
    case CHIP_ESP32S2:
        strncpy(info->chip_name, "ESP32-S2", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "ESP32-S2", sizeof(info->chip_model) - 1);
        break;
    case CHIP_ESP32S3:
        strncpy(info->chip_name, "ESP32-S3", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "ESP32-S3", sizeof(info->chip_model) - 1);
        break;
    case CHIP_ESP32C3:
        strncpy(info->chip_name, "ESP32-C3", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "ESP32-C3", sizeof(info->chip_model) - 1);
        break;
    case CHIP_ESP32C6:
        strncpy(info->chip_name, "ESP32-C6", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "ESP32-C6", sizeof(info->chip_model) - 1);
        break;
    case CHIP_ESP32H2:
        strncpy(info->chip_name, "ESP32-H2", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "ESP32-H2", sizeof(info->chip_model) - 1);
        break;
    default:
        strncpy(info->chip_name, "ESP32-Unknown", sizeof(info->chip_name) - 1);
        strncpy(info->chip_model, "Unknown", sizeof(info->chip_model) - 1);
        break;
    }

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    snprintf(info->flash_size, sizeof(info->flash_size), "%luMB", (unsigned long)(flash_size / (1024 * 1024)));

    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(info->mac_address, sizeof(info->mac_address), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        strncpy(info->version, app_desc->version, sizeof(info->version) - 1);
    }

    info->cpu_freq_mhz = (uint32_t)(esp_clk_cpu_freq() / 1000000);

    // 读取温度传感器
    info->temp_sensor_supported = ble_srv_temp_sensor_is_supported() ? 1 : 0;
    if (info->temp_sensor_supported) {
        float temp = 0.0f;
        if (ble_srv_temp_sensor_read(&temp)) {
            info->temperature_celsius = temp;
            ESP_LOGI(TAG, "Temperature: %.2f°C", temp);
        } else {
            info->temperature_celsius = -999.0f; // 表示读取失败
            ESP_LOGW(TAG, "Failed to read temperature");
        }
    } else {
        info->temperature_celsius = -999.0f; // 表示不支持
        ESP_LOGI(TAG, "Temperature sensor not supported");
    }

    ESP_LOGI(TAG, "Device: %s, Flash: %s, MAC: %s, Ver: %s, CPU: %luMHz, Temp: %.2f°C",
             info->chip_name, info->flash_size, info->mac_address,
             info->version, (unsigned long)info->cpu_freq_mhz, info->temperature_celsius);

    return true;
}

bool ble_srv_get_memory_info(ble_srv_memory_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    info->heap_total = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    info->heap_free = esp_get_free_heap_size();
    info->heap_min_free = esp_get_minimum_free_heap_size();
    info->stack_high_watermark = (uint32_t)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);

#if CONFIG_SPIRAM
    info->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    info->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    info->psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
#else
    info->psram_total = 0;
    info->psram_free = 0;
    info->psram_min_free = 0;
#endif

    ESP_LOGI(TAG, "Memory: total=%lu, free=%lu, min_free=%lu, stack_hwm=%lu, psram_total=%lu, psram_free=%lu",
             (unsigned long)info->heap_total, (unsigned long)info->heap_free,
             (unsigned long)info->heap_min_free, (unsigned long)info->stack_high_watermark,
             (unsigned long)info->psram_total, (unsigned long)info->psram_free);

    return true;
}

bool ble_srv_get_cpu_info(ble_srv_cpu_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    info->cpu_freq_mhz = (uint32_t)(esp_clk_cpu_freq() / 1000000);
    info->cpu_usage = 0;
    info->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);

    ESP_LOGI(TAG, "CPU: freq=%luMHz, uptime=%lus",
             (unsigned long)info->cpu_freq_mhz, (unsigned long)info->uptime_seconds);

    return true;
}

bool ble_srv_get_flash_info(ble_srv_flash_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    info->flash_total = flash_size;

    info->partition_count = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (it) {
        while (it) {
            info->partition_count++;
            it = esp_partition_next(it);
        }
        esp_partition_iterator_release(it);
    }

    info->flash_free = 0;

    ESP_LOGI(TAG, "Flash: total=%lu, partitions=%d",
             (unsigned long)info->flash_total, info->partition_count);

    return true;
}

bool ble_srv_get_partition_info(uint8_t index, ble_srv_partition_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    uint8_t count = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    if (!it) {
        ESP_LOGW(TAG, "No partitions found");
        return false;
    }

    const esp_partition_t *part = NULL;
    while (it) {
        if (count == index) {
            part = esp_partition_get(it);
            break;
        }
        count++;
        it = esp_partition_next(it);
    }

    if (!part) {
        esp_partition_iterator_release(it);
        ESP_LOGW(TAG, "Partition index %d not found", index);
        return false;
    }

    strncpy(info->label, part->label, sizeof(info->label) - 1);
    info->address = part->address;
    info->size = part->size;
    info->type = part->type;
    info->subtype = part->subtype;

    esp_partition_iterator_release(it);

    ESP_LOGI(TAG, "Partition[%d]: %s @0x%lx size=%lu type=%d subtype=%d",
             index, info->label, (unsigned long)info->address,
             (unsigned long)info->size, info->type, info->subtype);

    return true;
}

void ble_srv_restart_device(void)
{
    ESP_LOGI(TAG, "Restarting device...");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
}
