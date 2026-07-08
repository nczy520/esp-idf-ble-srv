#ifndef BLE_SRV_WIFI_H
#define BLE_SRV_WIFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_WIFI_SVC_UUID             0xFFC0
#define BLE_WIFI_CONFIG_CHAR_UUID     0xFFC1
#define BLE_WIFI_STATUS_CHAR_UUID     0xFFC2
#define BLE_WIFI_CTRL_CHAR_UUID       0xFFC3

typedef enum {
    BLE_WIFI_CTRL_FORGET = 0x01,
    BLE_WIFI_CTRL_NTP_SYNC = 0x02,
} ble_wifi_ctrl_cmd_t;

typedef struct __attribute__((packed)) {
    char ssid[33];
    char password[65];
} ble_wifi_config_t;

typedef struct __attribute__((packed)) {
    uint8_t connected;
    uint8_t rssi;
    uint32_t ip_address;
} ble_wifi_status_t;

#ifdef CONFIG_BLE_SRV_WIFI_PROVISIONER
bool ble_srv_wifi_provisioner_init(void);
bool ble_srv_wifi_is_connected(void);
void ble_srv_wifi_provisioner_deinit(void);
bool ble_srv_wifi_connect(const char *ssid, const char *password);
bool ble_srv_wifi_forget(void);
bool ble_srv_wifi_get_status(ble_wifi_status_t *status);
#endif

#ifdef CONFIG_BLE_SRV_NTP_SYNC
bool ble_srv_ntp_sync(void);
void ble_srv_ntp_deinit(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
