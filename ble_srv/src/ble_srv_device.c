#include "ble_srv_device.h"
#include "ble_srv_temp_sensor.h"
#include "ble_srv_ota_common.h"
#include "ble_srv.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_private/esp_clk.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ble_srv_log.h"

static const char *TAG = "BLE_SRV_DEVICE";

bool ble_srv_get_device_info(ble_srv_device_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    const char *chip_name = "ESP32-Unknown";
    switch (chip_info.model) {
    case CHIP_ESP32:     chip_name = "ESP32"; break;
    case CHIP_ESP32S2:   chip_name = "ESP32-S2"; break;
    case CHIP_ESP32S3:   chip_name = "ESP32-S3"; break;
    case CHIP_ESP32C3:   chip_name = "ESP32-C3"; break;
    case CHIP_ESP32C6:   chip_name = "ESP32-C6"; break;
    case CHIP_ESP32H2:   chip_name = "ESP32-H2"; break;
#if CONFIG_IDF_TARGET_ESP32P4
    case CHIP_ESP32P4:   chip_name = "ESP32-P4"; break;
#endif
#if CONFIG_IDF_TARGET_ESP32C5
    case CHIP_ESP32C5:   chip_name = "ESP32-C5"; break;
#endif
    default:             chip_name = "ESP32-Unknown"; break;
    }
    strncpy(info->chip_name, chip_name, sizeof(info->chip_name) - 1);
    strncpy(info->chip_model, chip_name, sizeof(info->chip_model) - 1);

    uint32_t flash_size = 0;
    esp_err_t ret = esp_flash_get_size(NULL, &flash_size);
    if (ret == ESP_OK && flash_size > 0) {
        snprintf(info->flash_size, sizeof(info->flash_size), "%luMB", (unsigned long)(flash_size / (1024 * 1024)));
    } else {
        snprintf(info->flash_size, sizeof(info->flash_size), "Unknown");
    }

    uint8_t mac[6] = {0};
    ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret == ESP_OK) {
        snprintf(info->mac_address, sizeof(info->mac_address), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
    } else {
        snprintf(info->mac_address, sizeof(info->mac_address), "Unknown");
    }

    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc) {
        strncpy(info->version, app_desc->version, sizeof(info->version) - 1);
    }

    info->cpu_freq_mhz = (uint32_t)(esp_clk_cpu_freq() / 1000000);
    info->cpu_cores = chip_info.cores;

    info->temp_sensor_supported = ble_srv_temp_sensor_is_supported() ? 1 : 0;
    if (info->temp_sensor_supported) {
        float temp = 0.0f;
        if (ble_srv_temp_sensor_read(&temp)) {
            info->temperature_celsius = temp;
            ESP_LOGI(TAG, "Temperature: %.2f°C", temp);
        } else {
            info->temperature_celsius = -999.0f;
            ESP_LOGW(TAG, "Failed to read temperature");
        }
    } else {
        info->temperature_celsius = -999.0f;
        ESP_LOGI(TAG, "Temperature sensor not supported");
    }

    info->reset_reason = (uint8_t)esp_reset_reason();
    info->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);

    time_t now;
    time(&now);
    info->current_time = (uint32_t)now;

    ESP_LOGI(TAG, "Device: %s, Flash: %s, MAC: %s, Ver: %s, CPU: %luMHz/%d cores, Temp: %.2f°C, Uptime: %lus, Reset: %d, Time: %lu",
             info->chip_name, info->flash_size, info->mac_address,
             info->version, (unsigned long)info->cpu_freq_mhz, info->cpu_cores, info->temperature_celsius,
             (unsigned long)info->uptime_seconds, info->reset_reason, (unsigned long)info->current_time);

    return true;
}

bool ble_srv_get_memory_info(ble_srv_memory_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    info->internal_total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->internal_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    info->internal_largest = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);

    info->dma_free = heap_caps_get_free_size(MALLOC_CAP_DMA);
    info->total_free = esp_get_free_heap_size();

    info->stack_hwm = (uint16_t)(uxTaskGetStackHighWaterMark(NULL) * (uint32_t)sizeof(StackType_t));

    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    info->task_count = (uint16_t)task_count;

#if CONFIG_SPIRAM
    info->psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    info->psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    info->psram_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
    info->psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
#else
    info->psram_total = 0;
    info->psram_free = 0;
    info->psram_min_free = 0;
    info->psram_largest = 0;
#endif

    ESP_LOGI(TAG, "Memory: INT[%lu/%lu min=%lu lg=%lu] DMA=%lu TOTAL=%lu PSRAM[%lu/%lu min=%lu lg=%lu] tasks=%u stack=%u",
             (unsigned long)info->internal_free, (unsigned long)info->internal_total,
             (unsigned long)info->internal_min_free, (unsigned long)info->internal_largest,
             (unsigned long)info->dma_free, (unsigned long)info->total_free,
             (unsigned long)info->psram_free, (unsigned long)info->psram_total,
             (unsigned long)info->psram_min_free, (unsigned long)info->psram_largest,
             info->task_count, info->stack_hwm);

    return true;
}

bool ble_srv_get_cpu_info(ble_srv_cpu_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    info->cpu_freq_mhz = (uint32_t)(esp_clk_cpu_freq() / 1000000);
    info->uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000);
    info->cpu_usage = 0;
    info->cpu_cores = chip_info.cores;
    info->features = chip_info.features;
    info->chip_revision = chip_info.revision;
    info->task_count = (uint16_t)uxTaskGetNumberOfTasks();

    const char *idf_ver = esp_get_idf_version();
    if (idf_ver) {
        strncpy(info->idf_version, idf_ver, sizeof(info->idf_version) - 1);
    }

    ESP_LOGI(TAG, "CPU: freq=%luMHz/%d cores rev%d, uptime=%lus, features=0x%lx, tasks=%u, IDF=%s",
             (unsigned long)info->cpu_freq_mhz, info->cpu_cores, info->chip_revision,
             (unsigned long)info->uptime_seconds, (unsigned long)info->features,
             info->task_count, info->idf_version);

    return true;
}

bool ble_srv_get_flash_info(ble_srv_flash_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    uint32_t flash_size = 0;
    esp_err_t ret = esp_flash_get_size(NULL, &flash_size);
    if (ret != ESP_OK) {
        flash_size = 0;
    }
    info->flash_total = flash_size;

    info->partition_count = 0;
    uint64_t used = 0;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t *part = esp_partition_get(it);
        if (part) {
            info->partition_count++;
            used += part->size;
        }
        it = esp_partition_next(it);
    }

    if (flash_size > used) {
        info->flash_free = flash_size - (uint32_t)used;
    } else {
        info->flash_free = 0;
    }

    const char *freq_str = CONFIG_ESPTOOLPY_FLASHFREQ;
    if (freq_str) {
        int freq = atoi(freq_str);
        info->flash_speed_mhz = (freq > 0) ? (uint8_t)freq : 80;
    } else {
        info->flash_speed_mhz = 80;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        strncpy(info->running_partition, running->label, sizeof(info->running_partition) - 1);
    }

    ESP_LOGI(TAG, "Flash: total=%lu, used=%llu, free=%lu, partitions=%d, speed=%dMHz, running=%s",
             (unsigned long)info->flash_total, (unsigned long long)used,
             (unsigned long)info->flash_free, info->partition_count,
             info->flash_speed_mhz, info->running_partition);

    return true;
}

bool ble_srv_get_partition_info(uint8_t index, ble_srv_partition_info_t *info)
{
    if (!info) {
        return false;
    }

    memset(info, 0, sizeof(*info));

    uint8_t count = 0;
    const esp_partition_t *found = NULL;
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        if (count == index) {
            found = esp_partition_get(it);
            break;
        }
        count++;
        it = esp_partition_next(it);
    }

    if (!found) {
        ESP_LOGW(TAG, "Partition index %d not found", index);
        return false;
    }

    strncpy(info->label, found->label, sizeof(info->label) - 1);
    info->address = found->address;
    info->size = found->size;
    info->type = found->type;
    info->subtype = found->subtype;

    ESP_LOGI(TAG, "Partition[%d]: %s @0x%lx size=%lu type=%d subtype=%d",
             index, info->label, (unsigned long)info->address,
             (unsigned long)info->size, info->type, info->subtype);

    return true;
}

void ble_srv_restart_device(void)
{
    ESP_LOGI(TAG, "Restarting device...");
    BLE_SRV_LOGI(TAG, "Device restart requested");
    ble_srv_schedule_restart(BLE_RESTART_DELAY_MS);
}

void ble_srv_device_deinit(void)
{
}
