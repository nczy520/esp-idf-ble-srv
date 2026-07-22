#include "ble_srv_msg.h"
#include "ble_srv_core.h"
#include "ble_srv_gatt.h"
#include "ble_srv_ota_common.h"
#include "ble_srv_ota_bt.h"
#include "ble_srv_ota_url.h"
#include "ble_srv_led.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/portmacro.h"
#include "esp_log.h"

static const char *TAG = "BLE_SRV_TASK";

static QueueHandle_t s_msg_queue = NULL;
static TaskHandle_t s_srv_task = NULL;

static ble_srv_msg_t s_msg_pool[BLE_SRV_MSG_POOL_SIZE];
static SemaphoreHandle_t s_pool_mutex = NULL;

static bool msg_pool_alloc(ble_srv_msg_t **msg);
static void msg_pool_free(ble_srv_msg_t *msg);
static void ble_srv_task(void *arg);
static void dispatch_msg(ble_srv_msg_t *msg);

bool ble_srv_msg_init(void)
{
    s_pool_mutex = xSemaphoreCreateMutex();
    if (!s_pool_mutex) {
        BLE_SRV_LOGE(TAG, "Failed to create pool mutex");
        return false;
    }

    for (int i = 0; i < BLE_SRV_MSG_POOL_SIZE; i++) {
        s_msg_pool[i].type = MSG_NONE;
    }

    s_msg_queue = xQueueCreate(BLE_SRV_QUEUE_LEN, sizeof(ble_srv_msg_t *));
    if (!s_msg_queue) {
        BLE_SRV_LOGE(TAG, "Failed to create message queue");
        vSemaphoreDelete(s_pool_mutex);
        return false;
    }

    return true;
}

void ble_srv_msg_deinit(void)
{
    if (s_msg_queue) {
        vQueueDelete(s_msg_queue);
        s_msg_queue = NULL;
    }
    if (s_pool_mutex) {
        vSemaphoreDelete(s_pool_mutex);
        s_pool_mutex = NULL;
    }
}

static bool msg_pool_alloc(ble_srv_msg_t **msg)
{
    xSemaphoreTake(s_pool_mutex, portMAX_DELAY);
    for (int i = 0; i < BLE_SRV_MSG_POOL_SIZE; i++) {
        if (s_msg_pool[i].type == MSG_NONE) {
            *msg = &s_msg_pool[i];
            (*msg)->type = 0xFFFF;
            xSemaphoreGive(s_pool_mutex);
            return true;
        }
    }
    xSemaphoreGive(s_pool_mutex);
    return false;
}

static void msg_pool_free(ble_srv_msg_t *msg)
{
    if (msg) {
        msg->type = MSG_NONE;
    }
}

bool ble_srv_msg_send(ble_srv_msg_type_t type, const uint8_t *data, uint16_t data_len,
                      uint16_t attr_handle, uint16_t conn_handle, TickType_t wait)
{
    bool is_isr = (xTaskGetCurrentTaskHandle() == NULL);

    ble_srv_msg_t *msg;
    // 任务上下文允许在 wait 时限内轮询等待池槽回收，避免池瞬时耗尽导致
    // 关键消息（尤其 OTA 无响应写命令）被静默丢弃。ISR 上下文不可阻塞。
    if (!msg_pool_alloc(&msg)) {
        bool got = false;
        if (!is_isr && wait > 0) {
            const TickType_t step = pdMS_TO_TICKS(5) ? pdMS_TO_TICKS(5) : 1;
            TickType_t waited = 0;
            while (waited < wait) {
                TickType_t d = (wait - waited) < step ? (wait - waited) : step;
                vTaskDelay(d);
                waited += d;
                if (msg_pool_alloc(&msg)) {
                    got = true;
                    break;
                }
            }
        }
        if (!got) {
            BLE_SRV_LOGW(TAG, "Message pool exhausted");
            return false;
        }
    }

    msg->type = type;
    msg->attr_handle = attr_handle;
    msg->conn_handle = conn_handle;
    msg->data_len = data_len > BLE_SRV_MSG_DATA_MAX_LEN ? BLE_SRV_MSG_DATA_MAX_LEN : data_len;
    if (data && data_len > 0) {
        memcpy(msg->data, data, msg->data_len);
    }

    BaseType_t ok;
    if (is_isr) {
        BaseType_t hpw;
        ok = xQueueSendFromISR(s_msg_queue, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    } else {
        ok = xQueueSend(s_msg_queue, &msg, wait);
    }

    if (ok != pdTRUE) {
        msg_pool_free(msg);
        BLE_SRV_LOGW(TAG, "Queue full, dropping message type=%d", type);
        return false;
    }

    return true;
}

void ble_srv_task_start(void)
{
    if (s_srv_task) {
        return;
    }

    BaseType_t ret = xTaskCreate(ble_srv_task, "ble_srv", BLE_SRV_TASK_STACK,
                                  NULL, BLE_SRV_TASK_PRIO, &s_srv_task);
    if (ret != pdPASS) {
        BLE_SRV_LOGE(TAG, "Failed to create ble_srv task");
    }
}

void ble_srv_task_stop(void)
{
    if (s_srv_task) {
        vTaskDelete(s_srv_task);
        s_srv_task = NULL;
    }
}

static void ble_srv_task(void *arg)
{
    (void)arg;
    BLE_SRV_LOGI(TAG, "ble_srv task started");

    ble_srv_msg_t *msg;
    while (xQueueReceive(s_msg_queue, &msg, portMAX_DELAY) == pdTRUE) {
        dispatch_msg(msg);
        msg_pool_free(msg);
    }

    BLE_SRV_LOGI(TAG, "ble_srv task exiting");
}

static void dispatch_msg(ble_srv_msg_t *msg)
{
    switch (msg->type) {
    case MSG_GAP_CONNECT:
        ble_srv_core_handle_connect(msg->conn_handle);
        break;
    case MSG_GAP_DISCONNECT:
        ble_srv_core_handle_disconnect(msg->conn_handle);
        break;
    case MSG_GAP_ADV_COMPLETE:
        ble_srv_core_handle_adv_complete();
        break;
    case MSG_GAP_SUBSCRIBE:
        ble_srv_core_handle_subscribe(msg->attr_handle, msg->conn_handle, msg->data, msg->data_len);
        break;
    case MSG_GAP_MTU:
        ble_srv_core_handle_mtu(msg->conn_handle, *(uint16_t *)msg->data);
        break;
    case MSG_GATT_WRITE:
        ble_srv_gatt_handle_write(msg->conn_handle, msg->attr_handle, msg->data, msg->data_len);
        break;
    case MSG_GATT_READ:
        ble_srv_gatt_handle_read(msg->conn_handle, msg->attr_handle);
        break;
    case MSG_OTA_BT_CMD:
        ble_srv_ota_bt_dispatch_cmd(msg->data, msg->data_len);
        break;
    case MSG_OTA_BT_FW_DATA:
        ble_srv_ota_bt_process_fw_data(msg->data, msg->data_len);
        break;
    case MSG_OTA_URL_START:
        ble_srv_ota_url_start((const char *)msg->data);
        break;
    case MSG_OTA_URL_ABORT:
        ble_srv_ota_url_handle_abort();
        break;
    case MSG_OTA_RESET_TIMER:
        ble_srv_ota_reset_to_idle();
        break;
    case MSG_OTA_PUSH_STATUS:
        ble_srv_ota_push_status_internal();
        break;
    case MSG_LED_SET_ON:
        ble_srv_led_set_on(msg->data[0]);
        break;
    case MSG_LED_SET_RGB:
        ble_srv_led_set_rgb(msg->data[0], msg->data[1], msg->data[2]);
        break;
    case MSG_LED_SET_EFFECT:
        ble_srv_led_set_effect((ble_led_effect_t)msg->data[0], msg->data[1]);
        break;
    case MSG_SCHEDULE_RESTART:
        ble_srv_schedule_restart_internal(*(uint32_t *)msg->data);
        break;
    default:
        BLE_SRV_LOGW(TAG, "Unknown message type: %d", msg->type);
        break;
    }
}
