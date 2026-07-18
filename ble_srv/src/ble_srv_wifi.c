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
#include "ble_srv_log.h"

static const char *TAG = "BLE_SRV_WIFI";

#define BLE_WIFI_NVS_NAMESPACE "ble_wifi"
#define BLE_WIFI_NVS_KEY_SSID  "ssid"
#define BLE_WIFI_NVS_KEY_PASS  "pass"
#define BLE_WIFI_NVS_KEY_PASS_LEN "pass_len"
#define PROV_NVS_NAMESPACE     "wifi_prov"
#define PROV_NVS_KEY_SSID      "ssid"
#define PROV_NVS_KEY_PASS      "pass"

#define BLE_WIFI_PASS_MAX_LEN  65

static bool s_initialized = false;
static wifi_prov_config_t s_prov_config;

static uint8_t s_obf_key[6] = {0};
static bool s_obf_key_ready = false;

static void init_obf_key(void)
{
    if (s_obf_key_ready) return;
    uint8_t mac[6] = {0};
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read BT MAC for obf key, using Wi-Fi MAC");
        ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC for obf key, using fallback");
        const uint8_t fallback[6] = {0xAE, 0x5B, 0xC3, 0x7D, 0xF1, 0x29};
        memcpy(s_obf_key, fallback, 6);
    } else {
        for (int i = 0; i < 6; i++) {
            s_obf_key[i] = mac[i] ^ (mac[(i + 3) % 6] + 0x5A);
        }
    }
    s_obf_key_ready = true;
}

static void obf_xor(uint8_t *dst, const uint8_t *src, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        dst[i] = src[i] ^ s_obf_key[i % sizeof(s_obf_key)];
    }
}

static bool save_pass_obfuscated(nvs_handle_t handle, const char *password)
{
    size_t pass_len = strlen(password);
    if (pass_len >= BLE_WIFI_PASS_MAX_LEN) {
        pass_len = BLE_WIFI_PASS_MAX_LEN - 1;
    }

    uint8_t obf_buf[BLE_WIFI_PASS_MAX_LEN];
    memset(obf_buf, 0, sizeof(obf_buf));
    obf_xor(obf_buf, (const uint8_t *)password, pass_len);

    esp_err_t err = nvs_set_blob(handle, BLE_WIFI_NVS_KEY_PASS, obf_buf, BLE_WIFI_PASS_MAX_LEN);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save obfuscated password: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_u16(handle, BLE_WIFI_NVS_KEY_PASS_LEN, (uint16_t)pass_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password length: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

static bool save_to_prov_nvs(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROV_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open prov NVS: %s", esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(handle, PROV_NVS_KEY_SSID, ssid);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save SSID to prov NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_set_str(handle, PROV_NVS_KEY_PASS, password);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save password to prov NVS: %s", esp_err_to_name(err));
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit prov NVS: %s", esp_err_to_name(err));
        return false;
    }

    return true;
}

static void destroy_orphan_sta_netif(void)
{
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        ESP_LOGI(TAG, "Destroying orphan STA netif before wifi_prov_start");
        esp_netif_destroy_default_wifi(sta_netif);
    }
}

bool ble_srv_wifi_provisioner_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "WiFi provisioner already initialized");
        return true;
    }

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS version mismatch, erasing NVS");
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(erase_ret));
            return false;
        }
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s", esp_err_to_name(ret));
        return false;
    }

    init_obf_key();

    s_prov_config = (wifi_prov_config_t)WIFI_PROV_DEFAULT_CONFIG();

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi provisioner initialized");
    BLE_SRV_LOGI(TAG, "WiFi provisioner initialized");
    return true;
}

const char *ble_srv_wifi_get_ap_ssid(void)
{
    return s_prov_config.ap_ssid ? s_prov_config.ap_ssid : "";
}

const char *ble_srv_wifi_get_ap_password(void)
{
    return s_prov_config.ap_password ? s_prov_config.ap_password : "";
}

bool ble_srv_wifi_is_connected(void)
{
    return wifi_prov_is_connected();
}

bool ble_srv_wifi_auto_connect(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "WiFi provisioner not initialized");
        return false;
    }

    if (wifi_prov_is_connected()) {
        ESP_LOGI(TAG, "WiFi already connected");
        BLE_SRV_LOGI(TAG, "WiFi already connected");
        return true;
    }

    ESP_LOGI(TAG, "Auto-connecting to saved WiFi...");
    BLE_SRV_LOGI(TAG, "Auto-connecting to saved WiFi...");

    destroy_orphan_sta_netif();

    esp_err_t err = wifi_prov_start(&s_prov_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_start failed: %s", esp_err_to_name(err));
        return false;
    }

    s_initialized = true;
    return true;
}

void ble_srv_wifi_provisioner_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    wifi_prov_stop();
    s_initialized = false;
    ESP_LOGI(TAG, "WiFi provisioner deinitialized");
    BLE_SRV_LOGI(TAG, "WiFi provisioner deinitialized");
}

bool ble_srv_wifi_connect(const char *ssid, const char *password)
{
    if (!ssid || !password) {
        ESP_LOGE(TAG, "Invalid SSID or password");
        return false;
    }

    ESP_LOGI(TAG, "Connecting to WiFi: %s", ssid);
    BLE_SRV_LOGI(TAG, "Connecting to WiFi: %s", ssid);

    init_obf_key();

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

    if (!save_pass_obfuscated(handle, password)) {
        nvs_close(handle);
        return false;
    }

    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
        return false;
    }

    if (!save_to_prov_nvs(ssid, password)) {
        ESP_LOGE(TAG, "Failed to save credentials to provisioner NVS");
        return false;
    }

    if (!s_initialized) {
        ble_srv_wifi_provisioner_init();
    }

    wifi_prov_stop();
    s_initialized = false;

    destroy_orphan_sta_netif();

    err = wifi_prov_start(&s_prov_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_start failed: %s", esp_err_to_name(err));
        BLE_SRV_LOGE(TAG, "wifi_prov_start failed: %s", esp_err_to_name(err));
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "WiFi provisioner started with new credentials");
    BLE_SRV_LOGI(TAG, "WiFi provisioner started with new credentials");
    return true;
}

bool ble_srv_wifi_forget(void)
{
    if (!s_initialized) {
        ESP_LOGW(TAG, "WiFi not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Forgetting WiFi credentials");
    BLE_SRV_LOGI(TAG, "Forgetting WiFi credentials");

    wifi_prov_stop();
    s_initialized = false;

    esp_err_t err = wifi_prov_erase_credentials();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase credentials: %s", esp_err_to_name(err));
        return false;
    }

    nvs_handle_t handle;
    err = nvs_open(BLE_WIFI_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_erase_all(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase all NVS: %s", esp_err_to_name(err));
        } else {
            err = nvs_commit(handle);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
            }
        }
        nvs_close(handle);
    } else {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
    }

    esp_wifi_disconnect();
    esp_wifi_stop();

    ESP_LOGI(TAG, "WiFi credentials erased, disconnected");
    BLE_SRV_LOGI(TAG, "WiFi credentials erased, disconnected");
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
