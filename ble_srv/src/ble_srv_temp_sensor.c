#include "ble_srv_temp_sensor.h"
#include "esp_log.h"

static const char *TAG = "BLE_SRV_TEMP";

#if SOC_TEMP_SENSOR_SUPPORTED
#include "driver/temperature_sensor.h"

static temperature_sensor_handle_t s_temp_sensor = NULL;
static bool s_temp_sensor_initialized = false;

bool ble_srv_temp_sensor_init(void)
{
    if (s_temp_sensor_initialized) {
        ESP_LOGW(TAG, "Temperature sensor already initialized");
        return true;
    }

    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(CONFIG_BLE_SRV_TEMP_SENSOR_RANGE_MIN, CONFIG_BLE_SRV_TEMP_SENSOR_RANGE_MAX);
    esp_err_t ret = temperature_sensor_install(&temp_sensor_config, &s_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install temperature sensor: %s", esp_err_to_name(ret));
        return false;
    }

    ret = temperature_sensor_enable(s_temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable temperature sensor: %s", esp_err_to_name(ret));
        temperature_sensor_uninstall(s_temp_sensor);
        s_temp_sensor = NULL;
        return false;
    }

    s_temp_sensor_initialized = true;
    ESP_LOGI(TAG, "Temperature sensor initialized");
    return true;
}

bool ble_srv_temp_sensor_read(float *out_celsius)
{
    if (!s_temp_sensor_initialized || s_temp_sensor == NULL) {
        ESP_LOGE(TAG, "Temperature sensor not initialized");
        return false;
    }

    esp_err_t ret = temperature_sensor_get_celsius(s_temp_sensor, out_celsius);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

void ble_srv_temp_sensor_deinit(void)
{
    if (!s_temp_sensor_initialized || s_temp_sensor == NULL) {
        return;
    }

    temperature_sensor_disable(s_temp_sensor);
    temperature_sensor_uninstall(s_temp_sensor);
    s_temp_sensor = NULL;
    s_temp_sensor_initialized = false;
    ESP_LOGI(TAG, "Temperature sensor deinitialized");
}

bool ble_srv_temp_sensor_is_supported(void)
{
    return true;
}

#else // !SOC_TEMP_SENSOR_SUPPORTED

bool ble_srv_temp_sensor_init(void)
{
    ESP_LOGW(TAG, "Temperature sensor not supported on this chip");
    return false;
}

bool ble_srv_temp_sensor_read(float *out_celsius)
{
    (void)out_celsius;
    ESP_LOGW(TAG, "Temperature sensor not supported on this chip");
    return false;
}

void ble_srv_temp_sensor_deinit(void)
{
    // Nothing to do
}

bool ble_srv_temp_sensor_is_supported(void)
{
    return false;
}

#endif // SOC_TEMP_SENSOR_SUPPORTED
