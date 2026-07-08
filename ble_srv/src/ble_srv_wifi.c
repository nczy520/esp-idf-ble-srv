#include "ble_srv.h"
#include "ble_srv_wifi.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifi_provisioner.h"

static const char *TAG = "BLE_SRV_WIFI";

#define BLE_WIFI_NVS_NAMESPACE "ble_wifi"
#define BLE_WIFI_NVS_KEY_SSID  "ssid"
#define BLE_WIFI_NVS_KEY_PASS  "pass"

static bool s_initialized = false;
static wifi_prov_config_t s_prov_config;

bool ble_srv_wifi_provisioner_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "WiFi provisioner already initialized");
        return true;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    s_prov_config = (wifi_prov_config_t)WIFI_PROV_DEFAULT_CONFIG();

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi provisioner initialized");
    return true;
}

bool ble_srv_wifi_is_connected(void)
{
    return wifi_prov_is_connected();
}

void ble_srv_wifi_provisioner_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    wifi_prov_stop();
    s_initialized = false;
    ESP_LOGI(TAG, "WiFi provisioner deinitialized");
}

bool ble_srv_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid SSID or password");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(BLE_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, BLE_WIFI_NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_set_str(handle, BLE_WIFI_NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }

    if (!s_initialized) {
        ble_srv_wifi_provisioner_init();
    }

    wifi_prov_stop();

    err = wifi_prov_start(&s_prov_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_start failed: %s", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "WiFi provisioner started with new credentials");
    return true;
}

bool ble_srv_wifi_forget(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "WiFi not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Forgetting WiFi credentials");

    wifi_prov_stop();

    esp_err_t err = wifi_prov_erase_credentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase credentials: %s", esp_err_to_name(err));
        return false;
    }

    nvs_handle_t handle;
    err = nvs_open(BLE_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        nvs_erase_all(handle);
        nvs_commit(handle);
        nvs_close(handle);
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

    ESP_LOGI(TAG, "WiFi credentials erased, disconnected");
    return true;
}

bool ble_srv_wifi_get_status(ble_wifi_status_t *status)
{
    if (!status) {
        return false;
    }

    memset(status, 0, sizeof(*status));

    status->connected = wifi_prov_is_connected() ? 1 : 0;

    if (status->connected) {
        esp_netif_ip_info_t ip_info;
        esp_err_t err = wifi_prov_get_ip_info(&ip_info);
        if (err == ESP_OK) {
            status->ip_address = ip_info.ip.addr;
        }

        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            status->rssi = (uint8_t)(ap_info.rssi < 0 ? -ap_info.rssi : ap_info.rssi);
        }
    }

    ESP_LOGI(TAG, "WiFi status: connected=%d, rssi=%d, ip=%lu",
             status->connected, status->rssi, (unsigned long)status->ip_address);

    return true;
}
