#include "ble_srv.h"
#include "ble_srv_gatt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_led.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_SRV";

#define BLE_SRV_NAME_PREFIX         CONFIG_BLE_SRV_ADV_NAME_PREFIX
#define BLE_SRV_ADV_INTERVAL_MIN    (CONFIG_BLE_SRV_ADV_INTERVAL_MIN * 1000 / 625)
#define BLE_SRV_ADV_INTERVAL_MAX    (CONFIG_BLE_SRV_ADV_INTERVAL_MAX * 1000 / 625)

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_advertising = false;
static uint8_t s_own_addr_type = 0;

static char s_device_name[64] = {0};

static void ble_srv_start_advertising(void);
static int ble_srv_gap_event_handler(struct ble_gap_event *event, void *arg);

static void ble_srv_start_advertising(void)
{
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;
    struct ble_gap_adv_params adv_params;
    int rc;

    if (s_advertising) {
        ESP_LOGW(TAG, "Already advertising");
        return;
    }

    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: rc=%d", rc);
        return;
    }

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (uint8_t *)s_device_name;
    rsp_fields.name_len = strlen(s_device_name);
    rsp_fields.name_is_complete = 1;

    const struct ble_gatt_svc_def *svcs = ble_srv_get_gatt_svcs();
    if (svcs && svcs[0].uuid) {
        rsp_fields.uuids16 = (const ble_uuid16_t *)svcs[0].uuid;
        rsp_fields.num_uuids16 = 1;
        rsp_fields.uuids16_is_complete = 1;
    }

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_SRV_ADV_INTERVAL_MIN;
    adv_params.itvl_max = BLE_SRV_ADV_INTERVAL_MAX;

    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_srv_gap_event_handler, NULL);
    if (rc == 0) {
        s_advertising = true;
        ESP_LOGI(TAG, "BLE advertising started");
    } else if (rc == BLE_HS_EALREADY) {
        s_advertising = true;
        ESP_LOGW(TAG, "BLE already advertising");
    } else {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: rc=%d", rc);
    }
}

static int ble_srv_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "CONNECT: status=%d", event->connect.status);
        if (event->connect.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            s_conn_handle = event->connect.conn_handle;
            s_advertising = false;
        } else {
            ble_srv_start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "DISCONNECT: reason=%d", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_advertising = false;
        ble_srv_ota_bt_reset();
        vTaskDelay(pdMS_TO_TICKS(100));
        ble_srv_start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = false;
        ble_srv_start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "SUBSCRIBE: attr=%d, notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        if (event->subscribe.attr_handle == g_ota_status_chr_val_handle) {
            g_ota_status_notify_enabled = event->subscribe.cur_notify;
        }
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
        else if (event->subscribe.attr_handle == g_wifi_status_chr_val_handle) {
            g_wifi_status_notify_enabled = event->subscribe.cur_notify;
        }
#endif
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU: conn=%d, mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

static void ble_srv_on_sync(void)
{
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: rc=%d", rc);
        return;
    }

    uint8_t addr[6] = {0};
    rc = ble_hs_id_copy_addr(s_own_addr_type, addr, NULL);
    if (rc == 0) {
        ESP_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X",
                 addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }

    ble_srv_start_advertising();
}

static void ble_srv_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
}

static void ble_srv_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool ble_srv_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE Service (NimBLE)");

    uint8_t mac[6];
    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_read_mac failed: %s", esp_err_to_name(ret));
        return false;
    }

    int name_len = snprintf(s_device_name, sizeof(s_device_name), "%s-%02X%02X%02X",
                             BLE_SRV_NAME_PREFIX, mac[3], mac[4], mac[5]);
    if (name_len < 0 || name_len >= (int)sizeof(s_device_name)) {
        ESP_LOGE(TAG, "Device name overflow");
        return false;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        return false;
    }

    ble_hs_cfg.reset_cb = ble_srv_on_reset;
    ble_hs_cfg.sync_cb = ble_srv_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_svc_gap_device_name_set(s_device_name);
    if (rc != 0) {
        ESP_LOGW(TAG, "ble_svc_gap_device_name_set failed: rc=%d", rc);
    }

    const struct ble_gatt_svc_def *svcs = ble_srv_get_gatt_svcs();
    rc = ble_gatts_count_cfg(svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: rc=%d", rc);
        nimble_port_deinit();
        return false;
    }

    rc = ble_gatts_add_svcs(svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        nimble_port_deinit();
        return false;
    }

    nimble_port_freertos_init(ble_srv_host_task);

    ble_srv_ota_bt_init();

#ifdef CONFIG_BLE_SRV_LED_ENABLED
    ble_srv_led_init();
#endif

    ESP_LOGI(TAG, "BLE Service ready, device=%s", s_device_name);
    return true;
}

void ble_srv_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE Service");

    ble_srv_ota_bt_reset();

#ifdef CONFIG_BLE_SRV_LED_ENABLED
    ble_srv_led_deinit();
#endif

    nimble_port_stop();
    nimble_port_deinit();

    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_advertising = false;
}

bool ble_srv_is_connected(void)
{
    return s_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}

uint16_t ble_srv_get_conn_handle(void)
{
    return s_conn_handle;
}
