#include "ble_srv.h"
#include "ble_srv_gatt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_led.h"
#include "ble_srv_temp_sensor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "ble_srv_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_SRV";

#define BLE_SRV_NAME_PREFIX         CONFIG_BLE_SRV_ADV_NAME_PREFIX
#define BLE_SRV_ADV_INTERVAL_MIN    (CONFIG_BLE_SRV_ADV_INTERVAL_MIN * 1000 / 625)
#define BLE_SRV_ADV_INTERVAL_MAX    (CONFIG_BLE_SRV_ADV_INTERVAL_MAX * 1000 / 625)

static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static volatile bool s_advertising = false;
static uint8_t s_own_addr_type = 0;
static SemaphoreHandle_t s_state_lock = NULL;

static char s_device_name[64] = {0};
static TimerHandle_t s_restart_timer = NULL;

#define STATE_LOCK_TIMEOUT_MS    100
#define STATE_LOCK()   do { if (s_state_lock) xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(STATE_LOCK_TIMEOUT_MS)); } while(0)
#define STATE_UNLOCK() do { if (s_state_lock) xSemaphoreGive(s_state_lock); } while(0)

static void restart_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    esp_restart();
}

static void ble_srv_start_advertising(void);
static int ble_srv_gap_event_handler(struct ble_gap_event *event, void *arg);

static void ble_srv_start_advertising(void)
{
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;
    struct ble_gap_adv_params adv_params;
    int rc;

    STATE_LOCK();
    if (s_advertising) {
        STATE_UNLOCK();
        ESP_LOGW(TAG, "Already advertising");
        return;
    }
    STATE_UNLOCK();

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
    STATE_LOCK();
    if (rc == 0) {
        s_advertising = true;
        STATE_UNLOCK();
        ESP_LOGI(TAG, "BLE advertising started");
        BLE_SRV_LOGI(TAG, "BLE advertising started");
    } else if (rc == BLE_HS_EALREADY) {
        s_advertising = true;
        STATE_UNLOCK();
        ESP_LOGW(TAG, "BLE already advertising");
        BLE_SRV_LOGW(TAG, "BLE already advertising");
    } else {
        STATE_UNLOCK();
        ESP_LOGE(TAG, "ble_gap_adv_start failed: rc=%d", rc);
        BLE_SRV_LOGE(TAG, "ble_gap_adv_start failed: rc=%d", rc);
    }
}

static int ble_srv_gap_event_handler(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "CONNECT: status=%d", event->connect.status);
        BLE_SRV_LOGI(TAG, "CONNECT: status=%d", event->connect.status);
        STATE_LOCK();
        if (event->connect.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            s_conn_handle = event->connect.conn_handle;
            s_advertising = false;
            STATE_UNLOCK();
            ble_srv_gatt_clear_auth_state(event->connect.conn_handle);
            ble_srv_gatt_set_log_conn_handle(event->connect.conn_handle);
        } else {
            STATE_UNLOCK();
            ble_srv_start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "DISCONNECT: reason=%d", event->disconnect.reason);
        BLE_SRV_LOGI(TAG, "DISCONNECT: reason=%d", event->disconnect.reason);
        STATE_LOCK();
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_advertising = false;
        STATE_UNLOCK();
        ble_srv_gatt_clear_auth_state(event->disconnect.conn.conn_handle);
        ble_srv_gatt_set_log_conn_handle(BLE_HS_CONN_HANDLE_NONE);
        ble_srv_gatt_set_log_notify_enabled(false);
        ble_srv_gatt_set_ota_status_notify_enabled(false);
        ble_srv_gatt_set_wifi_status_notify_enabled(false);
        ble_srv_ota_abort(BLE_OTA_ERR_DISCONNECTED);
#ifdef CONFIG_BLE_SRV_LED_ENABLED
        ble_srv_led_set_on(false);
#endif
        ble_srv_start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        STATE_LOCK();
        s_advertising = false;
        STATE_UNLOCK();
        ble_srv_start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "SUBSCRIBE: attr=%d, notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        BLE_SRV_LOGI(TAG, "SUBSCRIBE: attr=%d, notify=%d",
                      event->subscribe.attr_handle, event->subscribe.cur_notify);
        if (event->subscribe.attr_handle == ble_srv_gatt_get_ota_status_chr_val_handle()) {
            ble_srv_gatt_set_ota_status_notify_enabled(event->subscribe.cur_notify);
        }
#ifdef CONFIG_BLE_SRV_WIFI_ENABLED
        else if (event->subscribe.attr_handle == ble_srv_gatt_get_wifi_status_chr_val_handle()) {
            ble_srv_gatt_set_wifi_status_notify_enabled(event->subscribe.cur_notify);
        }
#endif
        else if (event->subscribe.attr_handle == ble_srv_gatt_get_log_chr_val_handle()) {
            ble_srv_gatt_set_log_notify_enabled(event->subscribe.cur_notify);
            if (event->subscribe.cur_notify) {
                ble_srv_gatt_set_log_conn_handle(event->subscribe.conn_handle);
            }
            ESP_LOGI(TAG, "LOG notify %s (conn=%d)",
                     event->subscribe.cur_notify ? "enabled" : "disabled",
                     event->subscribe.conn_handle);
        }
        else if (event->subscribe.attr_handle == ble_srv_gatt_get_custom_cmd_chr_val_handle()) {
            ble_srv_gatt_set_custom_cmd_notify_enabled(event->subscribe.cur_notify);
            ESP_LOGI(TAG, "Custom cmd notify %s (conn=%d)",
                     event->subscribe.cur_notify ? "enabled" : "disabled",
                     event->subscribe.conn_handle);
        }
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
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: rc=%d", rc);
        return;
    }

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
        BLE_SRV_LOGI(TAG, "BLE address: %02X:%02X:%02X:%02X:%02X:%02X",
                      addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
    }

    ble_srv_start_advertising();
}

static void ble_srv_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
    BLE_SRV_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
}

static void ble_srv_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool ble_srv_init(void)
{
    ESP_LOGI(TAG, "Initializing BLE Service (NimBLE)");
    BLE_SRV_LOGI(TAG, "Initializing BLE Service (NimBLE)");

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

    s_state_lock = xSemaphoreCreateMutex();
    if (!s_state_lock) {
        ESP_LOGE(TAG, "Failed to create state lock");
        return false;
    }

    if (!ble_srv_gatt_init_lock()) {
        ESP_LOGE(TAG, "Failed to create GATT lock");
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }

    ble_hs_cfg.reset_cb = ble_srv_on_reset;
    ble_hs_cfg.sync_cb = ble_srv_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_store_clear();
    ESP_LOGI(TAG, "BLE bond store cleared");

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
        ble_srv_gatt_deinit();
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }

    rc = ble_gatts_add_svcs(svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: rc=%d", rc);
        nimble_port_deinit();
        ble_srv_gatt_deinit();
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }

    nimble_port_freertos_init(ble_srv_host_task);

    if (!ble_srv_ota_init()) {
        ESP_LOGE(TAG, "Failed to initialize OTA common");
        nimble_port_stop();
        nimble_port_deinit();
        ble_srv_gatt_deinit();
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }

    if (!ble_srv_ota_bt_init()) {
        ESP_LOGE(TAG, "Failed to initialize BT OTA");
        ble_srv_ota_deinit();
        nimble_port_stop();
        nimble_port_deinit();
        ble_srv_gatt_deinit();
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }

    if (!ble_srv_ota_url_init()) {
        ESP_LOGW(TAG, "URL OTA init failed (non-critical)");
    }

#ifdef CONFIG_BLE_SRV_LED_ENABLED
    if (!ble_srv_led_init()) {
        ESP_LOGE(TAG, "Failed to initialize LED");
        ble_srv_ota_url_deinit();
        ble_srv_ota_bt_deinit();
        ble_srv_ota_deinit();
        nimble_port_stop();
        nimble_port_deinit();
        ble_srv_gatt_deinit();
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        return false;
    }
#endif

    if (!ble_srv_temp_sensor_init()) {
        ESP_LOGW(TAG, "Temperature sensor init failed (non-critical)");
    }

    ESP_LOGI(TAG, "BLE Service ready, device=%s", s_device_name);
    BLE_SRV_LOGI(TAG, "BLE Service ready, device=%s", s_device_name);
    return true;
}

void ble_srv_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing BLE Service");
    BLE_SRV_LOGI(TAG, "Deinitializing BLE Service");

    ble_srv_ota_abort(BLE_OTA_ERR_ABORTED);
    vTaskDelay(pdMS_TO_TICKS(BLE_OTA_DEINIT_WAIT_MS));

#ifdef CONFIG_BLE_SRV_LED_ENABLED
    ble_srv_led_deinit();
#endif

    ble_srv_temp_sensor_deinit();

    ble_srv_ota_url_deinit();
    ble_srv_ota_bt_deinit();
    ble_srv_ota_deinit();

    nimble_port_stop();
    nimble_port_deinit();

    ble_srv_gatt_deinit();

    STATE_LOCK();
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_advertising = false;
    STATE_UNLOCK();

    if (s_restart_timer) {
        xTimerStop(s_restart_timer, 0);
        xTimerDelete(s_restart_timer, portMAX_DELAY);
        s_restart_timer = NULL;
    }

    if (s_state_lock) {
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
    }
}

bool ble_srv_is_connected(void)
{
    STATE_LOCK();
    bool connected = (s_conn_handle != BLE_HS_CONN_HANDLE_NONE);
    STATE_UNLOCK();
    return connected;
}

uint16_t ble_srv_get_conn_handle(void)
{
    STATE_LOCK();
    uint16_t handle = s_conn_handle;
    STATE_UNLOCK();
    return handle;
}

void ble_srv_schedule_restart(uint32_t delay_ms)
{
    BLE_SRV_LOGI(TAG, "Device restart scheduled in %lu ms", (unsigned long)delay_ms);

    if (!s_restart_timer) {
        s_restart_timer = xTimerCreate("srv_rboot", pdMS_TO_TICKS(delay_ms),
                                        pdFALSE, NULL, restart_timer_cb);
        if (s_restart_timer) {
            xTimerStart(s_restart_timer, 0);
        }
        return;
    }
    xTimerChangePeriod(s_restart_timer, pdMS_TO_TICKS(delay_ms), 0);
    xTimerReset(s_restart_timer, 0);
    xTimerStart(s_restart_timer, 0);
}
