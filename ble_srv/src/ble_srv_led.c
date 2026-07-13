#include "ble_srv_led.h"
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "hal/gpio_types.h"

static const char *TAG = "BLE_SRV_LED";

#if defined(CONFIG_BLE_SRV_LED_GPIO)
#define BLE_LED_GPIO            CONFIG_BLE_SRV_LED_GPIO
#elif CONFIG_IDF_TARGET_ESP32S3
#define BLE_LED_GPIO            21
#elif CONFIG_IDF_TARGET_ESP32C3 || CONFIG_IDF_TARGET_ESP32C6 || CONFIG_IDF_TARGET_ESP32H2
#define BLE_LED_GPIO            10
#elif CONFIG_IDF_TARGET_ESP32S2
#define BLE_LED_GPIO            18
#else
#define BLE_LED_GPIO            2
#endif

#define RMT_RESOLUTION_HZ       10000000
#define WS2812_T0H_TICKS        3
#define WS2812_T0L_TICKS        9
#define WS2812_T1H_TICKS        9
#define WS2812_T1L_TICKS        3
#define WS2812_RESET_TICKS      2800

#define LED_TASK_STACK          4096
#define LED_TASK_PRIO           5
#define LED_TX_WAIT_MS          100
#define LED_EFFECT_TICK_MS      20
#define LED_STROBE_ON_MS        10
#define LED_LOCK_RETRY_MS       10
#define LED_DEINIT_WAIT_MS      200
#define LED_SEND_OFF_DELAY_MS   20
#define LED_STOP_EFFECT_WAIT_MS 100
#define LED_DEFAULT_SPEED       50
#define LED_LOCK_TIMEOUT_MS     5000

#define LED_EFFECT_NOTIFY_STOP     (1 << 0)
#define LED_EFFECT_NOTIFY_RESTART  (1 << 1)

static rmt_channel_handle_t s_led_chan = NULL;
static rmt_encoder_handle_t s_copy_encoder = NULL;
static bool s_initialized = false;

static SemaphoreHandle_t s_lock = NULL;
static bool s_led_on = false;
static uint8_t s_red = 0;
static uint8_t s_green = 0;
static uint8_t s_blue = 0;
static ble_led_effect_t s_effect = BLE_LED_EFFECT_NONE;
static uint8_t s_speed = 50;

static TaskHandle_t s_effect_task = NULL;

static void ble_srv_led_send_pixel(uint8_t red, uint8_t green, uint8_t blue);

static void ws2812_rgb_to_rmt_symbols(uint8_t red, uint8_t green, uint8_t blue,
                                        rmt_symbol_word_t *symbols)
{
    uint32_t rgb = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    for (int i = 23; i >= 0; i--) {
        int idx = 23 - i;
        if (rgb & (1 << i)) {
            symbols[idx].duration0 = WS2812_T1H_TICKS;
            symbols[idx].level0 = 1;
            symbols[idx].duration1 = WS2812_T1L_TICKS;
            symbols[idx].level1 = 0;
        } else {
            symbols[idx].duration0 = WS2812_T0H_TICKS;
            symbols[idx].level0 = 1;
            symbols[idx].duration1 = WS2812_T0L_TICKS;
            symbols[idx].level1 = 0;
        }
    }
    symbols[24].duration0 = WS2812_RESET_TICKS;
    symbols[24].level0 = 0;
    symbols[24].duration1 = 0;
    symbols[24].level1 = 0;
}

static void ble_srv_led_send_pixel(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_initialized || !s_led_chan || !s_copy_encoder) {
        return;
    }

    rmt_symbol_word_t symbols[25];
    ws2812_rgb_to_rmt_symbols(red, green, blue, symbols);

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };

    rmt_transmit(s_led_chan, s_copy_encoder, symbols, sizeof(symbols), &tx_config);
    rmt_tx_wait_all_done(s_led_chan, pdMS_TO_TICKS(LED_TX_WAIT_MS));
}

static void effect_breath(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    float brightness = 0.0f;
    float step = (float)speed / 5000.0f;
    int direction = 1;

    while (1) {
        uint32_t notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_EFFECT_TICK_MS));
        if (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) {
            return;
        }

        brightness += step * direction;
        if (brightness >= 1.0f) {
            brightness = 1.0f;
            direction = -1;
        } else if (brightness <= 0.0f) {
            brightness = 0.0f;
            direction = 1;
        }

        uint8_t cr = (uint8_t)(r * brightness);
        uint8_t cg = (uint8_t)(g * brightness);
        uint8_t cb = (uint8_t)(b * brightness);
        ble_srv_led_send_pixel(cr, cg, cb);
    }
}

static void effect_blink(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t delay_ms = 1000 / (speed > 0 ? speed : 1);

    while (1) {
        ble_srv_led_send_pixel(r, g, b);
        uint32_t notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
        if (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) {
            return;
        }

        ble_srv_led_send_pixel(0, 0, 0);
        notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
        if (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) {
            return;
        }
    }
}

static void effect_rainbow(uint8_t speed)
{
    float hue = 0.0f;
    float step = (float)speed / 50.0f;

    while (1) {
        uint32_t notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_EFFECT_TICK_MS));
        if (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) {
            return;
        }

        hue = fmodf(hue + step, 360.0f);

        float h = hue / 60.0f;
        float c = 1.0f;
        float x = 1.0f - fabsf(fmodf(h, 2.0f) - 1.0f);

        float r1 = 0, g1 = 0, b1 = 0;
        if (h < 1.0f) { r1 = c; g1 = x; b1 = 0; }
        else if (h < 2.0f) { r1 = x; g1 = c; b1 = 0; }
        else if (h < 3.0f) { r1 = 0; g1 = c; b1 = x; }
        else if (h < 4.0f) { r1 = 0; g1 = x; b1 = c; }
        else if (h < 5.0f) { r1 = x; g1 = 0; b1 = c; }
        else { r1 = c; g1 = 0; b1 = x; }

        ble_srv_led_send_pixel((uint8_t)(r1 * 255), (uint8_t)(g1 * 255), (uint8_t)(b1 * 255));
    }
}

static void effect_strobe(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t delay_ms = 1000 / (speed > 0 ? speed : 1);

    while (1) {
        ble_srv_led_send_pixel(r, g, b);
        uint32_t notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(LED_STROBE_ON_MS));
        if (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) {
            return;
        }

        ble_srv_led_send_pixel(0, 0, 0);
        notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(delay_ms));
        if (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) {
            return;
        }
    }
}

static void ble_srv_led_effect_task(void *arg)
{
    (void)arg;
    TaskHandle_t self_handle = xTaskGetCurrentTaskHandle();
    uint8_t r = 0, g = 0, b = 0, speed = LED_DEFAULT_SPEED;
    ble_led_effect_t effect = BLE_LED_EFFECT_NONE;
    ble_led_effect_t last_effect = BLE_LED_EFFECT_NONE;

    while (1) {
        if (s_effect_task != self_handle) {
            break;
        }

        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) == pdTRUE) {
            r = s_red;
            g = s_green;
            b = s_blue;
            speed = s_speed;
            effect = s_effect;
            xSemaphoreGive(s_lock);
        } else {
            ESP_LOGW(TAG, "Failed to take LED lock in effect task");
            vTaskDelay(pdMS_TO_TICKS(LED_LOCK_RETRY_MS));
            continue;
        }

        if (effect != last_effect) {
            ESP_LOGI(TAG, "Effect running: effect=%d, speed=%d", effect, speed);
            last_effect = effect;
        }

        switch (effect) {
        case BLE_LED_EFFECT_BREATH:
            effect_breath(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_BLINK:
            effect_blink(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_RAINBOW:
            effect_rainbow(speed);
            break;
        case BLE_LED_EFFECT_STROBE:
            effect_strobe(r, g, b, speed);
            break;
        default:
            break;
        }
    }

    s_effect_task = NULL;
    vTaskDelete(NULL);
}

static void ble_srv_led_start_effect(void)
{
    if (s_effect_task) {
        xTaskNotify(s_effect_task, LED_EFFECT_NOTIFY_RESTART, eSetBits);
        return;
    }

    BaseType_t ret = xTaskCreate(ble_srv_led_effect_task, "led_eff", LED_TASK_STACK, NULL, LED_TASK_PRIO, &s_effect_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create effect task");
        s_effect_task = NULL;
    }
}

static void ble_srv_led_stop_effect(uint32_t wait_ms)
{
    if (!s_effect_task) {
        return;
    }

    TaskHandle_t task = s_effect_task;
    s_effect_task = NULL;
    xTaskNotify(task, LED_EFFECT_NOTIFY_STOP, eSetBits);

    if (wait_ms > 0) {
        uint32_t waited = 0;
        while (eTaskGetState(task) != eDeleted && waited < wait_ms) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
        }
        if (eTaskGetState(task) != eDeleted) {
            vTaskDelete(task);
        }
    }
}

bool ble_srv_led_init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "LED already initialized");
        return true;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "Failed to create LED lock");
        return false;
    }

    rmt_tx_channel_config_t chan_cfg = {
        .gpio_num = BLE_LED_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };

    esp_err_t ret = rmt_new_tx_channel(&chan_cfg, &s_led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    rmt_copy_encoder_config_t copy_encoder_cfg = {};
    ret = rmt_new_copy_encoder(&copy_encoder_cfg, &s_copy_encoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create copy encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    ret = rmt_enable(s_led_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_copy_encoder);
        rmt_del_channel(s_led_chan);
        s_copy_encoder = NULL;
        s_led_chan = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    ble_srv_led_send_pixel(0, 0, 0);

    s_initialized = true;
    s_led_on = false;
    s_red = 0;
    s_green = 0;
    s_blue = 0;
    s_effect = BLE_LED_EFFECT_NONE;
    s_effect_task = NULL;

    ESP_LOGI(TAG, "WS2812 LED initialized on GPIO %d", BLE_LED_GPIO);
    return true;
}

void ble_srv_led_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    s_initialized = false;

    ble_srv_led_stop_effect(LED_DEINIT_WAIT_MS);

    vTaskDelay(pdMS_TO_TICKS(LED_SEND_OFF_DELAY_MS));

    ble_srv_led_send_pixel(0, 0, 0);

    if (s_copy_encoder) {
        rmt_del_encoder(s_copy_encoder);
        s_copy_encoder = NULL;
    }

    if (s_led_chan) {
        rmt_disable(s_led_chan);
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
    }

    if (s_lock) {
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
    }

    ESP_LOGI(TAG, "LED deinitialized");
}

bool ble_srv_led_set_on(bool on)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "LED not initialized");
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take LED lock in set_on");
        return false;
    }

    s_led_on = on;

    if (on) {
        if (s_effect != BLE_LED_EFFECT_NONE) {
            xSemaphoreGive(s_lock);
            ble_srv_led_start_effect();
        } else {
            uint8_t r = s_red, g = s_green, b = s_blue;
            xSemaphoreGive(s_lock);
            ble_srv_led_send_pixel(r, g, b);
        }
    } else {
        xSemaphoreGive(s_lock);
        ble_srv_led_stop_effect(LED_STOP_EFFECT_WAIT_MS);
        ble_srv_led_send_pixel(0, 0, 0);
    }

    ESP_LOGI(TAG, "LED %s", on ? "ON" : "OFF");
    return true;
}

bool ble_srv_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "LED not initialized");
        return false;
    }

    bool send_now = false;
    uint8_t r, g, b;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take LED lock in set_rgb");
        return false;
    }

    s_red = red;
    s_green = green;
    s_blue = blue;

    if (s_led_on && s_effect == BLE_LED_EFFECT_NONE) {
        send_now = true;
        r = red;
        g = green;
        b = blue;
    }

    xSemaphoreGive(s_lock);

    if (send_now) {
        ble_srv_led_send_pixel(r, g, b);
    }

    if (s_effect_task && s_led_on && s_effect != BLE_LED_EFFECT_NONE) {
        xTaskNotify(s_effect_task, LED_EFFECT_NOTIFY_RESTART, eSetBits);
    }

    ESP_LOGI(TAG, "LED RGB set: R=%d, G=%d, B=%d", red, green, blue);
    return true;
}

bool ble_srv_led_set_effect(ble_led_effect_t effect, uint8_t speed)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "LED not initialized");
        return false;
    }

    if (effect > BLE_LED_EFFECT_STROBE) {
        ESP_LOGW(TAG, "Invalid LED effect: %d", effect);
        return false;
    }

    if (speed == 0) {
        speed = 1;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take LED lock in set_effect");
        return false;
    }

    ble_led_effect_t old_effect = s_effect;
    s_effect = effect;
    s_speed = speed;
    bool led_on = s_led_on;
    uint8_t r = s_red, g = s_green, b = s_blue;

    xSemaphoreGive(s_lock);

    if (led_on && effect != BLE_LED_EFFECT_NONE) {
        if (old_effect == BLE_LED_EFFECT_NONE) {
            ble_srv_led_start_effect();
        } else if (s_effect_task) {
            xTaskNotify(s_effect_task, LED_EFFECT_NOTIFY_RESTART, eSetBits);
        }
    } else if (led_on) {
        ble_srv_led_stop_effect(LED_STOP_EFFECT_WAIT_MS);
        ble_srv_led_send_pixel(r, g, b);
    } else {
        ble_srv_led_stop_effect(LED_STOP_EFFECT_WAIT_MS);
    }

    ESP_LOGI(TAG, "LED effect set: %d, speed=%d", effect, speed);
    return true;
}

bool ble_srv_led_get_status(ble_led_status_t *status)
{
    if (!status || !s_initialized) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take LED lock in get_status");
        return false;
    }

    status->on = s_led_on ? 1 : 0;
    status->effect = (uint8_t)s_effect;
    status->speed = s_speed;
    status->red = s_red;
    status->green = s_green;
    status->blue = s_blue;

    xSemaphoreGive(s_lock);
    return true;
}
