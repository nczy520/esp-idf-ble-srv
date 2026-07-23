#include "ble_srv_led.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "hal/gpio_types.h"
#include "ble_srv_log.h"

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

#ifndef CONFIG_BLE_SRV_LED_ROWS
#define CONFIG_BLE_SRV_LED_ROWS    1
#endif
#ifndef CONFIG_BLE_SRV_LED_COLS
#define CONFIG_BLE_SRV_LED_COLS    1
#endif
#ifndef CONFIG_BLE_SRV_LED_COUNT_MAX
#define CONFIG_BLE_SRV_LED_COUNT_MAX 256
#endif

#define RMT_RESOLUTION_HZ       10000000
#define WS2812_T0H_TICKS        3
#define WS2812_T0L_TICKS        9
#define WS2812_T1H_TICKS        9
#define WS2812_T1L_TICKS        3
#define WS2812_RESET_TICKS      2800

#define LED_TASK_STACK          4096
#define LED_TASK_PRIO           5
#define LED_TX_WAIT_MS          200
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

/* 火焰效果使用的色彩冷却系数与衰减步进 */
#define LED_FIRE_COOLING   55
#define LED_FIRE_SPARKING  120

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

/* 矩阵布局：rows*cols 决定激活的 LED 数量。
 * 矩阵按蛇形（zigzag）布线：偶数行从左到右，奇数行从右到左。
 * 单灯/灯条场景 rows=1，退化为线性索引。
 */
static uint8_t s_led_rows = CONFIG_BLE_SRV_LED_ROWS;
static uint8_t s_led_cols = CONFIG_BLE_SRV_LED_COLS;
static int s_led_count = CONFIG_BLE_SRV_LED_ROWS * CONFIG_BLE_SRV_LED_COLS;

/* 帧缓冲与 RMT 符号缓冲：按 LED_COUNT_MAX 预分配，避免 effect 循环内 malloc。
 * s_pixel_buf：RGB 三字节一组，按 LED 索引顺序排列。
 * s_symbols：每个 LED 24 个 symbol + 末尾 1 个 reset。
 */
#define LED_COUNT_MAX         (CONFIG_BLE_SRV_LED_COUNT_MAX)
static uint8_t *s_pixel_buf = NULL;          /* LED_COUNT_MAX * 3 字节 */
static rmt_symbol_word_t *s_symbols = NULL;  /* (LED_COUNT_MAX * 24 + 1) 个 symbol */
static size_t s_pixel_buf_size = 0;
static size_t s_symbols_count = 0;

static TaskHandle_t s_effect_task = NULL;
static volatile bool s_effect_task_done = false;

/* ====================== 内部工具函数 ====================== */

static void ws2812_pixel_to_symbols(uint8_t red, uint8_t green, uint8_t blue,
                                     rmt_symbol_word_t *symbols)
{
    /* WS2812 颜色通道顺序为 RGB（按项目硬约束禁止 GRB） */
    uint32_t rgb = ((uint32_t)red << 16) | ((uint32_t)green << 8) | blue;
    for (int i = 23; i >= 0; i--) {
        int idx = 23 - i;
        if (rgb & (1UL << i)) {
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
}

static void ble_srv_led_clear_buf(void)
{
    if (s_pixel_buf && s_led_count > 0) {
        memset(s_pixel_buf, 0, (size_t)s_led_count * 3);
    }
}

static void ble_srv_led_set_pixel(int idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx < 0 || idx >= s_led_count || !s_pixel_buf) {
        return;
    }
    s_pixel_buf[idx * 3 + 0] = r;
    s_pixel_buf[idx * 3 + 1] = g;
    s_pixel_buf[idx * 3 + 2] = b;
}

static void ble_srv_led_fill_all(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_pixel_buf || s_led_count <= 0) {
        return;
    }
    for (int i = 0; i < s_led_count; i++) {
        s_pixel_buf[i * 3 + 0] = r;
        s_pixel_buf[i * 3 + 1] = g;
        s_pixel_buf[i * 3 + 2] = b;
    }
}

/* 将 (row, col) 映射到 LED 索引，支持蛇形布线。
 * 越界返回 -1。
 */
static int ble_srv_led_matrix_idx(int row, int col)
{
    if (s_led_rows <= 0 || s_led_cols <= 0) {
        return 0;
    }
    if (row < 0 || row >= s_led_rows || col < 0 || col >= s_led_cols) {
        return -1;
    }
    if ((row & 1) == 0) {
        return row * s_led_cols + col;
    }
    return row * s_led_cols + (s_led_cols - 1 - col);
}

static void ble_srv_led_send_frame(void)
{
    if (!s_led_chan || !s_copy_encoder || !s_pixel_buf || s_led_count <= 0) {
        return;
    }
    for (int i = 0; i < s_led_count; i++) {
        ws2812_pixel_to_symbols(s_pixel_buf[i * 3 + 0],
                                 s_pixel_buf[i * 3 + 1],
                                 s_pixel_buf[i * 3 + 2],
                                 &s_symbols[i * 24]);
    }
    size_t total = (size_t)s_led_count * 24;
    s_symbols[total].duration0 = WS2812_RESET_TICKS;
    s_symbols[total].level0 = 0;
    s_symbols[total].duration1 = 0;
    s_symbols[total].level1 = 0;

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
        .flags.eot_level = 0,
    };
    rmt_transmit(s_led_chan, s_copy_encoder, s_symbols,
                 ((size_t)s_led_count * 24 + 1) * sizeof(rmt_symbol_word_t),
                 &tx_config);
    rmt_tx_wait_all_done(s_led_chan, pdMS_TO_TICKS(LED_TX_WAIT_MS));
}

/* 等待 effect tick，返回 true 表示收到 stop/restart 信号应退出 */
static bool led_effect_wait_tick(uint32_t tick_ms)
{
    uint32_t notify = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(tick_ms));
    return (notify & (LED_EFFECT_NOTIFY_STOP | LED_EFFECT_NOTIFY_RESTART)) != 0;
}

/* HSV → RGB（hue: 0~359, sat/val: 0.0~1.0） */
static void hsv_to_rgb(float h, float s, float v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    float c = v * s;
    float hp = fmodf(h, 360.0f) / 60.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float r1 = 0, g1 = 0, b1 = 0;
    if (hp < 1.0f)      { r1 = c;  g1 = x;  b1 = 0; }
    else if (hp < 2.0f) { r1 = x;  g1 = c;  b1 = 0; }
    else if (hp < 3.0f) { r1 = 0;  g1 = c;  b1 = x; }
    else if (hp < 4.0f) { r1 = 0;  g1 = x;  b1 = c; }
    else if (hp < 5.0f) { r1 = x;  g1 = 0;  b1 = c; }
    else                { r1 = c;  g1 = 0;  b1 = x; }
    float m = v - c;
    *r = (uint8_t)((r1 + m) * 255.0f);
    *g = (uint8_t)((g1 + m) * 255.0f);
    *b = (uint8_t)((b1 + m) * 255.0f);
}

/* ====================== 效果实现 ====================== */

static void effect_breath(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    float brightness = 0.0f;
    float step = (float)speed / 5000.0f;
    int direction = 1;
    while (1) {
        if (led_effect_wait_tick(LED_EFFECT_TICK_MS)) return;
        brightness += step * direction;
        if (brightness >= 1.0f) { brightness = 1.0f; direction = -1; }
        else if (brightness <= 0.0f) { brightness = 0.0f; direction = 1; }
        uint8_t cr = (uint8_t)(r * brightness);
        uint8_t cg = (uint8_t)(g * brightness);
        uint8_t cb = (uint8_t)(b * brightness);
        ble_srv_led_fill_all(cr, cg, cb);
        ble_srv_led_send_frame();
    }
}

static void effect_blink(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t delay_ms = 1000 / (speed > 0 ? speed : 1);
    while (1) {
        ble_srv_led_fill_all(r, g, b);
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(delay_ms)) return;
        ble_srv_led_fill_all(0, 0, 0);
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(delay_ms)) return;
    }
}

static void effect_rainbow(uint8_t speed)
{
    float hue = 0.0f;
    float step = (float)speed / 50.0f;
    while (1) {
        if (led_effect_wait_tick(LED_EFFECT_TICK_MS)) return;
        hue = fmodf(hue + step, 360.0f);
        uint8_t r, g, b;
        hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
        ble_srv_led_fill_all(r, g, b);
        ble_srv_led_send_frame();
    }
}

static void effect_strobe(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t delay_ms = 1000 / (speed > 0 ? speed : 1);
    while (1) {
        ble_srv_led_fill_all(r, g, b);
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(LED_STROBE_ON_MS)) return;
        ble_srv_led_fill_all(0, 0, 0);
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(delay_ms)) return;
    }
}

/* 流光追逐：一颗彗星沿灯带移动，头部最亮，尾部渐暗 */
static void effect_chase(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    int tail_len = (s_led_count / 4) + 2;
    if (tail_len > 12) tail_len = 12;
    if (tail_len < 2) tail_len = 2;
    int pos = 0;
    uint32_t step_delay = 120 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int t = 0; t < tail_len; t++) {
            int idx = pos - t;
            if (idx < 0) idx += s_led_count;
            float fade = 1.0f - (float)t / (float)tail_len;
            uint8_t cr = (uint8_t)(r * fade);
            uint8_t cg = (uint8_t)(g * fade);
            uint8_t cb = (uint8_t)(b * fade);
            ble_srv_led_set_pixel(idx, cr, cg, cb);
        }
        ble_srv_led_send_frame();
        pos = (pos + 1) % s_led_count;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 色彩擦除：逐颗点亮全部 LED，再逐颗熄灭 */
static void effect_color_wipe(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 200 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        for (int i = 0; i < s_led_count; i++) {
            ble_srv_led_set_pixel(i, r, g, b);
            ble_srv_led_send_frame();
            if (led_effect_wait_tick(step_delay)) return;
        }
        for (int i = 0; i < s_led_count; i++) {
            ble_srv_led_set_pixel(i, 0, 0, 0);
            ble_srv_led_send_frame();
            if (led_effect_wait_tick(step_delay)) return;
        }
    }
}

/* 剧场追逐：每 3 颗亮一颗，整体向右移动 */
static void effect_theater_chase(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 150 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int phase = 0;
    while (1) {
        ble_srv_led_clear_buf();
        for (int i = 0; i < s_led_count; i++) {
            if ((i + phase) % 3 == 0) {
                ble_srv_led_set_pixel(i, r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase = (phase + 1) % 3;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 波浪：每个 LED 按位置正弦调制亮度，颜色随时间偏移色相 */
static void effect_wave(uint8_t speed)
{
    float phase = 0.0f;
    float phase_step = (float)speed / 200.0f;
    float hue_base = 0.0f;
    while (1) {
        if (led_effect_wait_tick(LED_EFFECT_TICK_MS)) return;
        for (int i = 0; i < s_led_count; i++) {
            float pos = (float)i / (float)(s_led_count > 1 ? s_led_count - 1 : 1);
            float wave = 0.5f * (1.0f + sinf(phase + pos * 2.0f * 3.14159265f * 2.0f));
            uint8_t r, g, b;
            hsv_to_rgb(hue_base + pos * 60.0f, 1.0f, wave, &r, &g, &b);
            ble_srv_led_set_pixel(i, r, g, b);
        }
        ble_srv_led_send_frame();
        phase += phase_step;
        hue_base = fmodf(hue_base + (float)speed / 30.0f, 360.0f);
    }
}

/* 流星：一颗带拖尾的亮光沿灯带快速移动 */
static void effect_meteor(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    int tail_len = (s_led_count / 3) + 4;
    if (tail_len > 16) tail_len = 16;
    if (tail_len < 4) tail_len = 4;
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int pos = 0;
    while (1) {
        /* 全部以背景黑为基础，仅渲染流星头部和拖尾 */
        ble_srv_led_clear_buf();
        for (int t = 0; t < tail_len; t++) {
            int idx = pos - t;
            if (idx < 0) idx += s_led_count;
            float fade = 1.0f - (float)t / (float)tail_len;
            fade = fade * fade;  /* 平方衰减，使头部更突出 */
            uint8_t cr = (uint8_t)(r * fade);
            uint8_t cg = (uint8_t)(g * fade);
            uint8_t cb = (uint8_t)(b * fade);
            ble_srv_led_set_pixel(idx, cr, cg, cb);
        }
        ble_srv_led_send_frame();
        pos = (pos + 1) % s_led_count;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 火焰：基于热力图的火焰闪烁效果。
 * 经典 FastLED Fire2012 风格的简化版：每帧根据热度生成红黄色火焰。
 */
static void effect_fire(uint8_t speed)
{
    uint8_t *heat = NULL;
    if (s_led_count > 0) {
        heat = (uint8_t *)malloc((size_t)s_led_count);
        if (!heat) return;
    }
    uint32_t step_delay = 120 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;

    while (1) {
        /* 步骤 1：冷却每个像素 */
        for (int i = 0; i < s_led_count; i++) {
            uint8_t cooldown = (uint8_t)(rand() % ((LED_FIRE_COOLING / 10) + 2));
            if (heat[i] > cooldown) {
                heat[i] -= cooldown;
            } else {
                heat[i] = 0;
            }
        }
        /* 步骤 2：从底部传播热量（向上漂移） */
        for (int k = s_led_count - 1; k >= 2; k--) {
            heat[k] = (uint8_t)((heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3);
        }
        /* 步骤 3：在底部随机点燃 */
        if (s_led_count > 0) {
            int spark = rand() % 256;
            if (spark < LED_FIRE_SPARKING) {
                int y = rand() % (s_led_count > 3 ? 3 : s_led_count);
                heat[y] = (heat[y] + 160 > 255) ? 255 : (uint8_t)(heat[y] + 160);
            }
        }
        /* 步骤 4：热度 → 颜色映射（黑→红→橙→黄→白） */
        for (int i = 0; i < s_led_count; i++) {
            uint8_t h = heat[i];
            /* 简化的 HeatColor(h) 近似：仅红到黄 */
            uint8_t r = h;
            uint8_t g = (h < 128) ? 0 : (uint8_t)((h - 128) * 2);
            uint8_t b = (h < 220) ? 0 : (uint8_t)((h - 220) * 8);
            ble_srv_led_set_pixel(i, r, g, b);
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) {
            free(heat);
            return;
        }
    }
}

/* 扫描：在矩阵上往返扫描一条亮线（按列扫描，颜色由 RGB 决定） */
static void effect_scan(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    /* 单行/单列时退化为线性扫描 */
    if (s_led_rows <= 1) {
        int pos = 0;
        int dir = 1;
        uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
        if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
        while (1) {
            ble_srv_led_clear_buf();
            ble_srv_led_set_pixel(pos, r, g, b);
            ble_srv_led_send_frame();
            pos += dir;
            if (pos >= s_led_count - 1) { pos = s_led_count - 1; dir = -1; }
            else if (pos <= 0) { pos = 0; dir = 1; }
            if (led_effect_wait_tick(step_delay)) return;
        }
    }

    int col = 0;
    int dir = 1;
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            int idx = ble_srv_led_matrix_idx(row, col);
            if (idx >= 0) {
                ble_srv_led_set_pixel(idx, r, g, b);
            }
        }
        ble_srv_led_send_frame();
        col += dir;
        if (col >= s_led_cols - 1) { col = s_led_cols - 1; dir = -1; }
        else if (col <= 0) { col = 0; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 跑马灯：多颗LED组成光点组移动 */
static void effect_marquee(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    int group_size = (s_led_count / 8) + 3;
    if (group_size > 8) group_size = 8;
    if (group_size < 2) group_size = 2;
    int pos = 0;
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int i = 0; i < group_size; i++) {
            int idx = (pos + i) % s_led_count;
            float fade = 1.0f - (float)i / (float)group_size;
            uint8_t cr = (uint8_t)(r * fade);
            uint8_t cg = (uint8_t)(g * fade);
            uint8_t cb = (uint8_t)(b * fade);
            ble_srv_led_set_pixel(idx, cr, cg, cb);
        }
        ble_srv_led_send_frame();
        pos = (pos + 1) % s_led_count;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 双色追逐：两种颜色交替追逐 */
static void effect_dual_chase(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    int tail_len = (s_led_count / 5) + 3;
    if (tail_len > 10) tail_len = 10;
    if (tail_len < 3) tail_len = 3;
    int pos1 = 0, pos2 = s_led_count / 2;
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int t = 0; t < tail_len; t++) {
            int idx1 = (pos1 - t + s_led_count) % s_led_count;
            int idx2 = (pos2 - t + s_led_count) % s_led_count;
            float fade = 1.0f - (float)t / (float)tail_len;
            ble_srv_led_set_pixel(idx1, (uint8_t)(r * fade), (uint8_t)(0), (uint8_t)(b * fade));
            ble_srv_led_set_pixel(idx2, (uint8_t)(0), (uint8_t)(g * fade), (uint8_t)(r * fade));
        }
        ble_srv_led_send_frame();
        pos1 = (pos1 + 1) % s_led_count;
        pos2 = (pos2 + 1) % s_led_count;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 像素雨：模拟下雨，LED从上往下掉落 */
static void effect_pixel_rain(uint8_t speed)
{
    uint8_t *drops = NULL;
    if (s_led_count > 0) {
        drops = (uint8_t *)calloc((size_t)s_led_count, sizeof(uint8_t));
        if (!drops) return;
    }
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = s_led_rows - 1; row >= 0; row--) {
            for (int col = 0; col < s_led_cols; col++) {
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx < 0) continue;
                if (drops[idx] > 0) {
                    uint8_t bright = (uint8_t)(drops[idx] * 8);
                    ble_srv_led_set_pixel(idx, 0, (uint8_t)(bright * 0.8), bright);
                    if (row < s_led_rows - 1) {
                        int below_idx = ble_srv_led_matrix_idx(row + 1, col);
                        if (below_idx >= 0 && drops[below_idx] == 0) {
                            drops[below_idx] = drops[idx];
                            drops[idx] = 0;
                        }
                    } else {
                        drops[idx] = 0;
                    }
                }
            }
        }
        if (rand() % 5 < 2) {
            int col = rand() % s_led_cols;
            int idx = ble_srv_led_matrix_idx(0, col);
            if (idx >= 0 && drops[idx] == 0) {
                drops[idx] = (uint8_t)(15 + rand() % 10);
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) {
            free(drops);
            return;
        }
    }
}

/* 随机闪烁：随机位置随机颜色闪烁 */
static void effect_random_blink(uint8_t speed)
{
    uint32_t step_delay = 50 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        int count = 1 + rand() % 5;
        for (int i = 0; i < count && i < s_led_count; i++) {
            int idx = rand() % s_led_count;
            uint8_t r = (uint8_t)(rand() % 256);
            uint8_t g = (uint8_t)(rand() % 256);
            uint8_t b = (uint8_t)(rand() % 256);
            ble_srv_led_set_pixel(idx, r, g, b);
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 渐变瀑布：颜色沿矩阵从上到下渐变流动 */
static void effect_gradient_fall(uint8_t speed)
{
    float hue_base = 0.0f;
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        for (int row = 0; row < s_led_rows; row++) {
            float hue = fmodf(hue_base + (float)row * 10.0f, 360.0f);
            uint8_t r, g, b;
            hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
            for (int col = 0; col < s_led_cols; col++) {
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx >= 0) {
                    float fade = 1.0f - (float)row / (float)s_led_rows;
                    ble_srv_led_set_pixel(idx, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
                }
            }
        }
        ble_srv_led_send_frame();
        hue_base = fmodf(hue_base + (float)speed / 5.0f, 360.0f);
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 心形：在矩阵上显示跳动的心形图案 */
static void effect_heart(uint8_t speed)
{
    uint32_t step_delay = 150 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float scale = 0.5f;
    int dir = 1;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float cx = (float)s_led_cols / 2.0f;
                float cy = (float)s_led_rows / 2.0f;
                float x = ((float)col - cx) / cx;
                float y = ((float)row - cy) / cy;
                float r = sqrtf(x * x + y * y);
                float angle = atan2f(y, x);
                float heart = powf(sinf(angle * r * scale * 5.0f), 3.0f) +
                              0.3f * cosf(angle * r * scale * 5.0f);
                if (heart > 0.1f) {
                    float intensity = 1.0f - heart;
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col),
                                          (uint8_t)(255 * intensity),
                                          (uint8_t)(80 * intensity),
                                          (uint8_t)(120 * intensity));
                }
            }
        }
        ble_srv_led_send_frame();
        scale += 0.05f * dir;
        if (scale >= 1.2f) { scale = 1.2f; dir = -1; }
        else if (scale <= 0.5f) { scale = 0.5f; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 棋盘格：棋盘格图案交替闪烁 */
static void effect_chessboard(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 200 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int phase = 0;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx < 0) continue;
                if ((row + col + phase) % 2 == 0) {
                    ble_srv_led_set_pixel(idx, r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
        phase = (phase + 1) % 2;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 光环：从中心向外扩散的光环效果 */
static void effect_ring(uint8_t speed)
{
    float radius = 0.0f;
    uint32_t step_delay = 50 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        float cx = (float)s_led_cols / 2.0f;
        float cy = (float)s_led_rows / 2.0f;
        float max_r = sqrtf(cx * cx + cy * cy);
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - cx;
                float dy = (float)row - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float ring = fabsf(dist - radius);
                if (ring < 1.5f) {
                    float fade = 1.0f - ring / 1.5f;
                    uint8_t r, g, b;
                    hsv_to_rgb(radius * 20.0f, 1.0f, fade, &r, &g, &b);
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
        radius += 0.5f;
        if (radius > max_r + 2.0f) radius = 0.0f;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 闪电：随机位置的闪电闪烁效果 */
static void effect_lightning(uint8_t speed)
{
    uint32_t step_delay = 1000 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        int flashes = 1 + rand() % 3;
        for (int f = 0; f < flashes; f++) {
            ble_srv_led_fill_all(255, 255, 255);
            ble_srv_led_send_frame();
            if (led_effect_wait_tick(LED_EFFECT_TICK_MS)) return;
            ble_srv_led_fill_all(0, 0, 0);
            ble_srv_led_send_frame();
            if (led_effect_wait_tick((uint32_t)(20 + rand() % 50))) return;
        }
        ble_srv_led_fill_all((uint8_t)(rand() % 50), (uint8_t)(rand() % 80), (uint8_t)(255));
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 爆炸：从中心向外扩散的爆炸效果 */
static void effect_explosion(uint8_t speed)
{
    uint32_t step_delay = 60 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        float radius = 0.0f;
        int center_row = s_led_rows / 2;
        int center_col = s_led_cols / 2;
        for (int iter = 0; iter < 15; iter++) {
            ble_srv_led_clear_buf();
            for (int row = 0; row < s_led_rows; row++) {
                for (int col = 0; col < s_led_cols; col++) {
                    float dx = (float)col - (float)center_col;
                    float dy = (float)row - (float)center_row;
                    float dist = sqrtf(dx * dx + dy * dy);
                    float ring = fabsf(dist - radius);
                    if (ring < 1.0f) {
                        float fade = 1.0f - radius / 10.0f;
                        if (fade < 0) fade = 0;
                        ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col),
                                              (uint8_t)(255 * fade),
                                              (uint8_t)(100 * fade),
                                              (uint8_t)(0));
                    }
                }
            }
            ble_srv_led_send_frame();
            radius += 0.8f;
            if (led_effect_wait_tick(step_delay)) return;
        }
        ble_srv_led_clear_buf();
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay * 3)) return;
    }
}

/* 雪花：模拟雪花飘落效果 */
static void effect_snow(uint8_t speed)
{
    int *snow_y = NULL;
    float *snow_x = NULL;
    float *snow_speed = NULL;
    int snow_count = s_led_cols / 2;
    if (snow_count < 3) snow_count = 3;
    if (snow_count > s_led_cols) snow_count = s_led_cols;
    snow_y = (int *)malloc((size_t)snow_count * sizeof(int));
    snow_x = (float *)malloc((size_t)snow_count * sizeof(float));
    snow_speed = (float *)malloc((size_t)snow_count * sizeof(float));
    if (!snow_y || !snow_x || !snow_speed) {
        free(snow_y); free(snow_x); free(snow_speed);
        return;
    }
    for (int i = 0; i < snow_count; i++) {
        snow_y[i] = -1 - rand() % 5;
        snow_x[i] = (float)(rand() % s_led_cols);
        snow_speed[i] = 0.3f + (float)rand() / 50.0f;
    }
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int i = 0; i < snow_count; i++) {
            snow_y[i] += 1;
            snow_x[i] += sinf((float)snow_y[i] * 0.1f) * 0.3f;
            if (snow_y[i] >= s_led_rows) {
                snow_y[i] = -1;
                snow_x[i] = (float)(rand() % s_led_cols);
            }
            int col = (int)snow_x[i];
            if (col < 0) col = 0;
            if (col >= s_led_cols) col = s_led_cols - 1;
            int idx = ble_srv_led_matrix_idx(snow_y[i], col);
            if (idx >= 0) {
                ble_srv_led_set_pixel(idx, 200, 220, 255);
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) {
            free(snow_y); free(snow_x); free(snow_speed);
            return;
        }
    }
}

/* 激光扫描：激光束往返扫描，带尾迹效果 */
static void effect_laser_scan(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    int pos = 0;
    int dir = 1;
    uint32_t step_delay = 40 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int tail = 0; tail < 8; tail++) {
            int idx = pos - tail * dir;
            if (idx < 0 || idx >= s_led_count) continue;
            float fade = 1.0f - (float)tail / 8.0f;
            ble_srv_led_set_pixel(idx, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
        }
        ble_srv_led_send_frame();
        pos += dir;
        if (pos >= s_led_count - 1) { pos = s_led_count - 1; dir = -1; }
        else if (pos <= 0) { pos = 0; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 水流：水流沿灯带流动效果 */
static void effect_waterflow(uint8_t speed)
{
    float phase = 0.0f;
    float phase_step = (float)speed / 100.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int i = 0; i < s_led_count; i++) {
            float wave = 0.5f * (1.0f + sinf(phase + (float)i * 0.3f));
            uint8_t r, g, b;
            hsv_to_rgb(180.0f + wave * 60.0f, 1.0f, wave, &r, &g, &b);
            ble_srv_led_set_pixel(i, r, g, b);
        }
        ble_srv_led_send_frame();
        phase += phase_step;
    }
}

/* 星星闪烁：模拟星星闪烁，随机亮度 */
static void effect_star_blink(uint8_t speed)
{
    uint8_t *brightness = NULL;
    if (s_led_count > 0) {
        brightness = (uint8_t *)malloc((size_t)s_led_count);
        if (!brightness) return;
        for (int i = 0; i < s_led_count; i++) {
            brightness[i] = (uint8_t)(rand() % 128);
        }
    }
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int i = 0; i < s_led_count; i++) {
            if (rand() % 20 == 0) {
                brightness[i] = (uint8_t)(rand() % 256);
            }
            if (brightness[i] > 0) {
                uint8_t r = (uint8_t)(brightness[i] * 0.8);
                uint8_t g = (uint8_t)(brightness[i] * 0.9);
                uint8_t b = brightness[i];
                ble_srv_led_set_pixel(i, r, g, b);
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) {
            free(brightness);
            return;
        }
    }
}

/* 极光：柔和的极光波动效果 */
static void effect_aurora(uint8_t speed)
{
    float phase1 = 0.0f, phase2 = 0.0f;
    float step1 = (float)speed / 500.0f, step2 = (float)speed / 700.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float x = (float)col / (float)s_led_cols;
                float y = (float)row / (float)s_led_rows;
                float wave1 = sinf(phase1 + x * 3.0f);
                float wave2 = sinf(phase2 + y * 2.0f);
                float bright = 0.3f + 0.7f * ((wave1 + wave2 + 2.0f) / 4.0f);
                uint8_t r, g, b;
                hsv_to_rgb(160.0f + wave1 * 30.0f, 0.8f, bright, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase1 += step1;
        phase2 += step2;
    }
}

/* 旋转彩虹：彩虹色轮在矩阵上旋转 */
static void effect_rotate_rainbow(uint8_t speed)
{
    float angle = 0.0f;
    float angle_step = (float)speed / 300.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        float cx = (float)s_led_cols / 2.0f;
        float cy = (float)s_led_rows / 2.0f;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - cx;
                float dy = (float)row - cy;
                float hue = fmodf(atan2f(dy, dx) * 180.0f / 3.14159265f + angle, 360.0f);
                uint8_t r, g, b;
                hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        angle += angle_step;
    }
}

/* 涟漪：像水滴落入水中的涟漪扩散 */
static void effect_ripple(uint8_t speed)
{
    float radius = 0.0f;
    uint32_t step_delay = 40 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        float cx = (float)s_led_cols / 2.0f;
        float cy = (float)s_led_rows / 2.0f;
        float max_r = sqrtf(cx * cx + cy * cy);
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - cx;
                float dy = (float)row - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float ripple = sinf((dist - radius) * 3.14159265f);
                if (ripple > 0) {
                    uint8_t r, g, b;
                    hsv_to_rgb(200.0f + ripple * 40.0f, 1.0f, ripple, &r, &g, &b);
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
        radius += 0.3f;
        if (radius > max_r + 3.0f) {
            radius = 0.0f;
        }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 马赛克：随机大小的色块闪烁 */
static void effect_mosaic(uint8_t speed)
{
    uint32_t step_delay = 200 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        int blocks = 3 + rand() % 6;
        for (int b = 0; b < blocks; b++) {
            int start_row = rand() % s_led_rows;
            int start_col = rand() % s_led_cols;
            int block_h = 1 + rand() % (s_led_rows - start_row);
            int block_w = 1 + rand() % (s_led_cols - start_col);
            uint8_t r = (uint8_t)(rand() % 256);
            uint8_t g = (uint8_t)(rand() % 256);
            uint8_t b = (uint8_t)(rand() % 256);
            for (int row = start_row; row < start_row + block_h; row++) {
                for (int col = start_col; col < start_col + block_w; col++) {
                    int idx = ble_srv_led_matrix_idx(row, col);
                    if (idx >= 0) {
                        ble_srv_led_set_pixel(idx, r, g, b);
                    }
                }
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 音频律动：根据速度参数模拟音乐节奏闪烁 */
static void effect_audio_rhythm(uint8_t speed)
{
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int beat = 0;
    while (1) {
        ble_srv_led_clear_buf();
        int intensity = (beat % 4 == 0) ? 255 : (beat % 2 == 0) ? 180 : 80;
        int pattern = beat % 8;
        for (int i = 0; i < s_led_count; i++) {
            float pos = (float)i / (float)s_led_count;
            float wave = sinf(pos * 3.14159265f * (float)(pattern + 2));
            uint8_t r = (uint8_t)((1.0f + wave) * 0.5f * intensity);
            uint8_t g = (uint8_t)((1.0f + sinf(pos * 3.14159265f * (float)(pattern + 3))) * 0.5f * intensity);
            uint8_t b = (uint8_t)((1.0f + sinf(pos * 3.14159265f * (float)(pattern + 4))) * 0.5f * intensity);
            ble_srv_led_set_pixel(i, r, g, b);
        }
        ble_srv_led_send_frame();
        beat++;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* ====================== 3x5 数字字体（用于时钟显示）====================== */
/* 每个数字 3列x5行，每行用3位表示（bit0=左, bit1=中, bit2=右）*/
static const uint8_t font3x5[11][5] = {
    {0x07, 0x05, 0x05, 0x05, 0x07},  /* 0 */
    {0x02, 0x06, 0x02, 0x02, 0x07},  /* 1 */
    {0x07, 0x01, 0x07, 0x04, 0x07},  /* 2 */
    {0x07, 0x01, 0x07, 0x01, 0x07},  /* 3 */
    {0x05, 0x05, 0x07, 0x01, 0x01},  /* 4 */
    {0x07, 0x04, 0x07, 0x01, 0x07},  /* 5 */
    {0x07, 0x04, 0x07, 0x05, 0x07},  /* 6 */
    {0x07, 0x01, 0x02, 0x02, 0x02},  /* 7 */
    {0x07, 0x05, 0x07, 0x05, 0x07},  /* 8 */
    {0x07, 0x05, 0x07, 0x01, 0x07},  /* 9 */
    {0x00, 0x02, 0x00, 0x02, 0x00},  /* : (索引10) */
};

/* 在矩阵上绘制一个3x5数字字符，offset_col为起始列 */
static void draw_digit(int digit, int offset_col, uint8_t r, uint8_t g, uint8_t b)
{
    if (digit < 0 || digit > 10) return;
    for (int row = 0; row < 5; row++) {
        uint8_t bits = font3x5[digit][row];
        for (int col = 0; col < 3; col++) {
            if (bits & (1 << col)) {
                int idx = ble_srv_led_matrix_idx(row, offset_col + col);
                if (idx >= 0) {
                    ble_srv_led_set_pixel(idx, r, g, b);
                }
            }
        }
    }
}

/* ====================== 矩阵扩展效果实现 ====================== */

/* 方块：方块在矩阵上弹跳 */
static void effect_block_bounce(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    float bx = 0, by = 0;
    float vx = 0.3f + (float)speed / 200.0f;
    float vy = 0.2f + (float)speed / 300.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    int bs = 2;
    if (s_led_cols < 4) bs = 1;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        ble_srv_led_clear_buf();
        int cx = (int)bx, cy = (int)by;
        for (int dy = 0; dy < bs; dy++) {
            for (int dx = 0; dx < bs; dx++) {
                int idx = ble_srv_led_matrix_idx(cy + dy, cx + dx);
                if (idx >= 0) ble_srv_led_set_pixel(idx, r, g, b);
            }
        }
        ble_srv_led_send_frame();
        bx += vx; by += vy;
        if (bx <= 0 || bx + bs >= s_led_cols) { vx = -vx; bx += vx; }
        if (by <= 0 || by + bs >= s_led_rows) { vy = -vy; by += vy; }
    }
}

/* 十字架：十字形图案随机闪现 */
static void effect_cross(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 300 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        int cx = rand() % s_led_cols;
        int cy = rand() % s_led_rows;
        for (int i = 0; i < s_led_cols; i++) {
            int idx = ble_srv_led_matrix_idx(cy, i);
            if (idx >= 0) ble_srv_led_set_pixel(idx, r, g, b);
        }
        for (int i = 0; i < s_led_rows; i++) {
            int idx = ble_srv_led_matrix_idx(i, cx);
            if (idx >= 0) ble_srv_led_set_pixel(idx, r, g, b);
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 螺旋：从中心向外螺旋扩散 */
static void effect_spiral(uint8_t speed)
{
    uint32_t step_delay = 60 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float angle = 0.0f;
    float radius = 0.0f;
    float cx = (float)s_led_cols / 2.0f;
    float cy = (float)s_led_rows / 2.0f;
    float max_r = sqrtf(cx * cx + cy * cy);
    while (1) {
        ble_srv_led_clear_buf();
        float a = angle, rad = radius;
        for (int i = 0; i < 30; i++) {
            int x = (int)(cx + cosf(a) * rad);
            int y = (int)(cy + sinf(a) * rad);
            int idx = ble_srv_led_matrix_idx(y, x);
            if (idx >= 0) {
                uint8_t r, g, b;
                hsv_to_rgb(a * 180.0f / 3.14159265f, 1.0f, 1.0f - i / 30.0f, &r, &g, &b);
                ble_srv_led_set_pixel(idx, r, g, b);
            }
            a += 0.5f;
            rad += 0.2f;
        }
        ble_srv_led_send_frame();
        angle += 0.3f;
        radius += 0.1f;
        if (radius > max_r) { radius = 0; angle = 0; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 菱形：菱形从小到大再消失 */
static void effect_diamond(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float size = 0.0f;
    int dir = 1;
    float cx = (float)s_led_cols / 2.0f - 0.5f;
    float cy = (float)s_led_rows / 2.0f - 0.5f;
    float max_size = (s_led_cols > s_led_rows ? s_led_cols : s_led_rows) / 2.0f + 2;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dist = fabsf((float)col - cx) + fabsf((float)row - cy);
                if (fabsf(dist - size) < 0.8f) {
                    float fade = 1.0f - size / max_size;
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col),
                                          (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
                }
            }
        }
        ble_srv_led_send_frame();
        size += 0.3f * dir;
        if (size >= max_size) { size = max_size; dir = -1; }
        else if (size <= 0) { size = 0; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 三角形：三角形图案扫描 */
static void effect_triangle(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int offset = 0;
    int dir = 1;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            int half_w = row + 1;
            int center = offset;
            for (int dx = -half_w; dx <= half_w; dx++) {
                int col = center + dx;
                if (col >= 0 && col < s_led_cols) {
                    int idx = ble_srv_led_matrix_idx(row, col);
                    if (idx >= 0) {
                        float fade = 1.0f - (float)row / (float)s_led_rows;
                        ble_srv_led_set_pixel(idx, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
                    }
                }
            }
        }
        ble_srv_led_send_frame();
        offset += dir;
        if (offset >= s_led_cols - 1 || offset <= 0) dir = -dir;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 圆形脉冲：同心圆从中心扩散 */
static void effect_circle_pulse(uint8_t speed)
{
    uint32_t step_delay = 40 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float radius = 0.0f;
    float cx = (float)s_led_cols / 2.0f - 0.5f;
    float cy = (float)s_led_rows / 2.0f - 0.5f;
    float max_r = sqrtf(cx * cx + cy * cy) + 2;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - cx;
                float dy = (float)row - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                for (int ring = 0; ring < 3; ring++) {
                    float r = radius - ring * 2.0f;
                    if (r < 0) continue;
                    if (fabsf(dist - r) < 0.8f) {
                        uint8_t cr, cg, cb;
                        hsv_to_rgb(r * 30.0f, 1.0f, 1.0f - (float)ring / 3.0f, &cr, &cg, &cb);
                        ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), cr, cg, cb);
                    }
                }
            }
        }
        ble_srv_led_send_frame();
        radius += 0.4f;
        if (radius > max_r) radius = 0;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 弹球：球在矩阵内弹跳变色 */
static void effect_ball(uint8_t speed)
{
    uint32_t step_delay = 60 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float bx = 1, by = 1;
    float vx = 0.5f + (float)speed / 200.0f;
    float vy = 0.4f + (float)speed / 250.0f;
    float hue = 0;
    while (1) {
        ble_srv_led_clear_buf();
        int cx = (int)bx, cy = (int)by;
        uint8_t r, g, b;
        hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx * dx + dy * dy <= 1) {
                    int idx = ble_srv_led_matrix_idx(cy + dy, cx + dx);
                    if (idx >= 0) ble_srv_led_set_pixel(idx, r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
        bx += vx; by += vy;
        if (bx <= 0 || bx >= s_led_cols - 1) { vx = -vx; hue = fmodf(hue + 60, 360); }
        if (by <= 0 || by >= s_led_rows - 1) { vy = -vy; hue = fmodf(hue + 60, 360); }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 贪吃蛇：经典贪吃蛇动画 */
static void effect_snake(uint8_t speed)
{
    uint32_t step_delay = 120 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int len = (s_led_count / 4) + 3;
    if (len > 20) len = 20;
    if (len < 3) len = 3;
    int *sx = (int *)malloc((size_t)len * sizeof(int));
    int *sy = (int *)malloc((size_t)len * sizeof(int));
    if (!sx || !sy) { free(sx); free(sy); return; }
    for (int i = 0; i < len; i++) { sx[i] = i; sy[i] = 0; }
    int dir = 0; /* 0=右 1=下 2=左 3=上 */
    int step_count = 0;
    while (1) {
        ble_srv_led_clear_buf();
        for (int i = 0; i < len; i++) {
            int idx = ble_srv_led_matrix_idx(sy[i], sx[i]);
            if (idx >= 0) {
                float fade = 1.0f - (float)i / (float)len;
                uint8_t r, g, b;
                hsv_to_rgb((float)i * 15.0f, 1.0f, fade, &r, &g, &b);
                ble_srv_led_set_pixel(idx, r, g, b);
            }
        }
        ble_srv_led_send_frame();
        int hx = sx[0], hy = sy[0];
        if (dir == 0) hx++;
        else if (dir == 1) hy++;
        else if (dir == 2) hx--;
        else hy--;
        if (hx < 0 || hx >= s_led_cols || hy < 0 || hy >= s_led_rows) {
            dir = (dir + 1) % 4;
            step_count = 0;
            continue;
        }
        for (int i = len - 1; i > 0; i--) { sx[i] = sx[i-1]; sy[i] = sy[i-1]; }
        sx[0] = hx; sy[0] = hy;
        step_count++;
        if (step_count > s_led_cols + s_led_rows) {
            dir = (dir + 1) % 4;
            step_count = 0;
        }
        if (led_effect_wait_tick(step_delay)) { free(sx); free(sy); return; }
    }
}

/* 生命游戏：康威生命游戏 */
static void effect_life(uint8_t speed)
{
    uint32_t step_delay = 300 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int total = s_led_rows * s_led_cols;
    if (total <= 0) return;
    uint8_t *cur = (uint8_t *)calloc((size_t)total, 1);
    uint8_t *nxt = (uint8_t *)calloc((size_t)total, 1);
    if (!cur || !nxt) { free(cur); free(nxt); return; }
    for (int i = 0; i < total; i++) {
        cur[i] = (rand() % 100 < 30) ? 1 : 0;
    }
    int generation = 0;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx >= 0 && cur[row * s_led_cols + col]) {
                    uint8_t r, g, b;
                    hsv_to_rgb((float)((generation * 5 + col * 3) % 360), 1.0f, 1.0f, &r, &g, &b);
                    ble_srv_led_set_pixel(idx, r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                int neighbors = 0;
                for (int dr = -1; dr <= 1; dr++) {
                    for (int dc = -1; dc <= 1; dc++) {
                        if (dr == 0 && dc == 0) continue;
                        int nr = row + dr, nc = col + dc;
                        if (nr >= 0 && nr < s_led_rows && nc >= 0 && nc < s_led_cols) {
                            neighbors += cur[nr * s_led_cols + nc];
                        }
                    }
                }
                int ci = row * s_led_cols + col;
                if (cur[ci]) nxt[ci] = (neighbors == 2 || neighbors == 3) ? 1 : 0;
                else nxt[ci] = (neighbors == 3) ? 1 : 0;
            }
        }
        memcpy(cur, nxt, (size_t)total);
        generation++;
        if (generation > 200) {
            for (int i = 0; i < total; i++) cur[i] = (rand() % 100 < 30) ? 1 : 0;
            generation = 0;
        }
        if (led_effect_wait_tick(step_delay)) { free(cur); free(nxt); return; }
    }
}

/* 字幕滚动：文字从右向左滚动（显示 "HELLO"） */
static void effect_scroll_text(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    /* 用 font3x5 拼出滚动数字 0-9 */
    uint32_t step_delay = 150 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int scroll_pos = s_led_cols;
    int digit = 0;
    int digit_counter = 0;
    while (1) {
        ble_srv_led_clear_buf();
        int col_offset = scroll_pos;
        draw_digit(digit, col_offset, r, g, b);
        draw_digit((digit + 1) % 10, col_offset + 4, r, g, b);
        draw_digit((digit + 2) % 10, col_offset + 8, r, g, b);
        ble_srv_led_send_frame();
        scroll_pos--;
        if (scroll_pos < -12) {
            scroll_pos = s_led_cols;
            digit_counter++;
            if (digit_counter >= 10) { digit_counter = 0; digit = (digit + 3) % 10; }
        }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 笑脸：笑脸图案眨眼 */
static void effect_smiley(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 200 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int blink = 0;
    int frame = 0;
    while (1) {
        ble_srv_led_clear_buf();
        int cx = s_led_cols / 2;
        int cy = s_led_rows / 2;
        /* 脸部轮廓（圆） */
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - (float)cx;
                float dy = (float)row - (float)cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float face_r = (s_led_cols < s_led_rows ? s_led_cols : s_led_rows) / 2.0f;
                if (fabsf(dist - face_r) < 0.8f) {
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, 0);
                }
            }
        }
        /* 眼睛 */
        if (!blink) {
            int idx1 = ble_srv_led_matrix_idx(cy - 1, cx - 2);
            int idx2 = ble_srv_led_matrix_idx(cy - 1, cx + 2);
            if (idx1 >= 0) ble_srv_led_set_pixel(idx1, 0, 0, b);
            if (idx2 >= 0) ble_srv_led_set_pixel(idx2, 0, 0, b);
        }
        /* 嘴巴（微笑弧线） */
        for (int dx = -2; dx <= 2; dx++) {
            int idx = ble_srv_led_matrix_idx(cy + 1, cx + dx);
            if (idx >= 0) ble_srv_led_set_pixel(idx, 0, g, 0);
        }
        ble_srv_led_send_frame();
        frame++;
        if (frame % 10 == 0) blink = !blink;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 海洋波浪：多层海浪叠加 */
static void effect_ocean(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 300.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float x = (float)col / (float)s_led_cols;
                float y = (float)row / (float)s_led_rows;
                float wave1 = sinf(phase + x * 6.0f) * 0.3f;
                float wave2 = sinf(phase * 1.3f + y * 4.0f) * 0.2f;
                float wave3 = sinf(phase * 0.7f + (x + y) * 5.0f) * 0.2f;
                float val = 0.5f + wave1 + wave2 + wave3;
                if (val < 0) val = 0;
                if (val > 1) val = 1;
                uint8_t r, g, b;
                hsv_to_rgb(180.0f + val * 40.0f, 0.8f, val, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 萤火虫：萤火虫随机飘动发光 */
static void effect_firefly(uint8_t speed)
{
    int count = s_led_count / 5;
    if (count < 3) count = 3;
    if (count > 20) count = 20;
    struct { float x, y, phase, brightness; } *flies;
    flies = malloc((size_t)count * sizeof(*flies));
    if (!flies) return;
    for (int i = 0; i < count; i++) {
        flies[i].x = (float)(rand() % s_led_cols);
        flies[i].y = (float)(rand() % s_led_rows);
        flies[i].phase = (float)(rand() % 360) * 3.14159265f / 180.0f;
        flies[i].brightness = 0;
    }
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    float step = (float)speed / 500.0f;
    while (1) {
        if (led_effect_wait_tick(step_delay)) { free(flies); return; }
        ble_srv_led_clear_buf();
        for (int i = 0; i < count; i++) {
            flies[i].x += sinf(flies[i].phase) * 0.1f;
            flies[i].y += cosf(flies[i].phase * 1.3f) * 0.1f;
            flies[i].phase += step;
            flies[i].brightness = 0.5f + 0.5f * sinf(flies[i].phase * 2.0f);
            if (flies[i].x < 0) flies[i].x = s_led_cols - 1;
            if (flies[i].x >= s_led_cols) flies[i].x = 0;
            if (flies[i].y < 0) flies[i].y = s_led_rows - 1;
            if (flies[i].y >= s_led_rows) flies[i].y = 0;
            int idx = ble_srv_led_matrix_idx((int)flies[i].y, (int)flies[i].x);
            if (idx >= 0) {
                uint8_t br = (uint8_t)(flies[i].brightness * 255);
                ble_srv_led_set_pixel(idx, (uint8_t)(br * 0.8f), br, (uint8_t)(br * 0.3f));
            }
        }
        ble_srv_led_send_frame();
    }
}

/* 岩浆：熔岩流动效果 */
static void effect_lava(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 400.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float x = (float)col / (float)s_led_cols;
                float y = (float)row / (float)s_led_rows;
                float v = sinf(phase + x * 5.0f + y * 3.0f) * 0.3f +
                          sinf(phase * 1.5f + (x + y) * 4.0f) * 0.2f + 0.5f;
                if (v < 0) v = 0;
                if (v > 1) v = 1;
                uint8_t r = (uint8_t)(v * 255);
                uint8_t g = (uint8_t)(v * v * 100);
                uint8_t b = 0;
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 云雾：柔和云雾飘动 */
static void effect_cloud(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 600.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float x = (float)col / (float)s_led_cols;
                float y = (float)row / (float)s_led_rows;
                float v = sinf(phase + x * 3.0f) * 0.2f +
                          sinf(phase * 1.2f + y * 2.5f) * 0.15f +
                          sinf(phase * 0.8f + (x + y) * 4.0f) * 0.1f + 0.4f;
                if (v < 0) v = 0;
                if (v > 1) v = 1;
                uint8_t c = (uint8_t)(v * 180);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), c, c, c);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 日出日落：太阳升降 */
static void effect_sunrise(uint8_t speed)
{
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float sun_y = 0;
    int dir = 1;
    while (1) {
        ble_srv_led_clear_buf();
        int sun_row = (int)(sun_y * (s_led_rows - 1));
        /* 天空渐变 */
        for (int row = 0; row < s_led_rows; row++) {
            float t = (float)row / (float)s_led_rows;
            uint8_t sky_r = (uint8_t)(t * 100 + 50);
            uint8_t sky_g = (uint8_t)(t * 80 + 30);
            uint8_t sky_b = (uint8_t)(t * 150 + 50);
            for (int col = 0; col < s_led_cols; col++) {
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx >= 0) ble_srv_led_set_pixel(idx, sky_r, sky_g, sky_b);
            }
        }
        /* 太阳 */
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx * dx + dy * dy <= 2) {
                    int idx = ble_srv_led_matrix_idx(sun_row + dy, s_led_cols / 2 + dx);
                    if (idx >= 0) ble_srv_led_set_pixel(idx, 255, 200, 0);
                }
            }
        }
        ble_srv_led_send_frame();
        sun_y += 0.02f * dir;
        if (sun_y >= 1.0f) { sun_y = 1.0f; dir = -1; }
        else if (sun_y <= 0) { sun_y = 0; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 星空：夜空星星闪烁 */
static void effect_starry(uint8_t speed)
{
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    uint8_t *star_b = (uint8_t *)calloc((size_t)s_led_count, 1);
    if (!star_b) return;
    while (1) {
        ble_srv_led_clear_buf();
        /* 深蓝背景 */
        for (int i = 0; i < s_led_count; i++) {
            ble_srv_led_set_pixel(i, 0, 0, 20);
        }
        /* 随机星星闪烁 */
        for (int i = 0; i < s_led_count; i++) {
            if (rand() % 30 == 0) {
                star_b[i] = (uint8_t)(128 + rand() % 128);
            }
            if (star_b[i] > 0) {
                ble_srv_led_set_pixel(i, star_b[i], star_b[i], (uint8_t)(star_b[i] * 0.8f));
                if (star_b[i] > 10) star_b[i] -= 10;
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) { free(star_b); return; }
    }
}

/* 分形：朱利亚集分形 */
static void effect_fractal(uint8_t speed)
{
    float zoom = 1.0f;
    int dir = 1;
    uint32_t step_delay = 200 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float zx = ((float)col / (float)s_led_cols - 0.5f) * 3.0f / zoom;
                float zy = ((float)row / (float)s_led_rows - 0.5f) * 3.0f / zoom;
                int iter = 0;
                int max_iter = 16;
                while (zx * zx + zy * zy < 4.0f && iter < max_iter) {
                    float tmp = zx * zx - zy * zy + 0.7f * cosf(zoom);
                    zy = 2.0f * zx * zy + 0.27015f;
                    zx = tmp;
                    iter++;
                }
                uint8_t r, g, b;
                hsv_to_rgb((float)iter * 22.0f, 1.0f, iter < max_iter ? 1.0f : 0.0f, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        zoom += 0.05f * dir;
        if (zoom > 3.0f) { zoom = 3.0f; dir = -1; }
        else if (zoom < 0.5f) { zoom = 0.5f; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 万花筒：对称万花筒图案 */
static void effect_kaleidoscope(uint8_t speed)
{
    float angle = 0.0f;
    float step = (float)speed / 200.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    int segments = 6;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        float cx = (float)s_led_cols / 2.0f;
        float cy = (float)s_led_rows / 2.0f;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - cx;
                float dy = (float)row - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float a = atan2f(dy, dx) + angle;
                float seg_a = fmodf(a, 2.0f * 3.14159265f / segments);
                if (seg_a > 3.14159265f / segments) seg_a = 2.0f * 3.14159265f / segments - seg_a;
                uint8_t r, g, b;
                hsv_to_rgb(seg_a * 180.0f / 3.14159265f + dist * 30.0f, 1.0f, 1.0f, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        angle += step;
    }
}

/* 全息图：全息投影彩虹波纹 */
static void effect_hologram(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 200.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        float cx = (float)s_led_cols / 2.0f;
        float cy = (float)s_led_rows / 2.0f;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx = (float)col - cx;
                float dy = (float)row - cy;
                float dist = sqrtf(dx * dx + dy * dy);
                float wave = sinf(dist * 2.0f - phase);
                uint8_t r, g, b;
                hsv_to_rgb(wave * 180.0f + 180.0f + dist * 20.0f, 1.0f, 0.5f + 0.5f * wave, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 全屏渐变：整屏颜色渐变流动 */
static void effect_gradient_full(uint8_t speed)
{
    float hue = 0.0f;
    float step = (float)speed / 50.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float h = fmodf(hue + (float)(row + col) * 5.0f, 360.0f);
                uint8_t r, g, b;
                hsv_to_rgb(h, 1.0f, 1.0f, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        hue += step;
    }
}

/* 对角线扫描：对角线扫描矩阵 */
static void effect_diagonal_scan(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 60 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int offset = 0;
    int dir = 1;
    int max_offset = s_led_rows + s_led_cols;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                if (row + col == offset) {
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
                }
                if (row + col == offset - 1) {
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col),
                                          (uint8_t)(r * 0.5f), (uint8_t)(g * 0.5f), (uint8_t)(b * 0.5f));
                }
            }
        }
        ble_srv_led_send_frame();
        offset += dir;
        if (offset >= max_offset) { offset = max_offset; dir = -1; }
        else if (offset <= 0) { offset = 0; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 网格脉冲：网格线从中心扩散 */
static void effect_grid_pulse(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 50 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float dist = 0.0f;
    int cx = s_led_cols / 2;
    int cy = s_led_rows / 2;
    float max_dist = (float)(cx > cy ? cx : cy) + 2;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float d = fabsf((float)(row - cy)) + fabsf((float)(col - cx));
                if (fabsf(d - dist) < 1.0f) {
                    float fade = 1.0f - dist / max_dist;
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col),
                                          (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
                }
            }
        }
        ble_srv_led_send_frame();
        dist += 0.5f;
        if (dist > max_dist) dist = 0;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 色块拼图：随机色块变换 */
static void effect_puzzle(uint8_t speed)
{
    uint32_t step_delay = 500 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int block_size = 3;
    if (s_led_cols < 6) block_size = 2;
    if (s_led_cols < 3) block_size = 1;
    while (1) {
        ble_srv_led_clear_buf();
        for (int by = 0; by < s_led_rows; by += block_size) {
            for (int bx = 0; bx < s_led_cols; bx += block_size) {
                uint8_t r = (uint8_t)(rand() % 256);
                uint8_t g = (uint8_t)(rand() % 256);
                uint8_t b = (uint8_t)(rand() % 256);
                for (int dy = 0; dy < block_size && by + dy < s_led_rows; dy++) {
                    for (int dx = 0; dx < block_size && bx + dx < s_led_cols; dx++) {
                        int idx = ble_srv_led_matrix_idx(by + dy, bx + dx);
                        if (idx >= 0) ble_srv_led_set_pixel(idx, r, g, b);
                    }
                }
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 圣诞树：圣诞树彩灯闪烁 */
static void effect_tree(uint8_t speed)
{
    uint32_t step_delay = 150 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int frame = 0;
    while (1) {
        ble_srv_led_clear_buf();
        int cx = s_led_cols / 2;
        for (int row = 0; row < s_led_rows - 1; row++) {
            int width = row + 1;
            if (width > cx + 1) width = cx + 1;
            for (int dx = -width; dx <= width; dx++) {
                int col = cx + dx;
                if (col >= 0 && col < s_led_cols) {
                    int idx = ble_srv_led_matrix_idx(row, col);
                    if (idx >= 0) {
                        if (rand() % 5 == 0) {
                            uint8_t r, g, b;
                            hsv_to_rgb((float)(rand() % 360), 1.0f, 1.0f, &r, &g, &b);
                            ble_srv_led_set_pixel(idx, r, g, b);
                        } else {
                            ble_srv_led_set_pixel(idx, 0, (uint8_t)(100 + row * 10), 0);
                        }
                    }
                }
            }
        }
        /* 树干 */
        for (int dx = -1; dx <= 1; dx++) {
            int idx = ble_srv_led_matrix_idx(s_led_rows - 1, cx + dx);
            if (idx >= 0) ble_srv_led_set_pixel(idx, 100, 50, 0);
        }
        /* 顶部星星 */
        int star_idx = ble_srv_led_matrix_idx(0, cx);
        if (star_idx >= 0 && frame % 4 < 2) {
            ble_srv_led_set_pixel(star_idx, 255, 255, 0);
        }
        ble_srv_led_send_frame();
        frame++;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 烟花：烟花升空爆炸 */
static void effect_firework(uint8_t speed)
{
    uint32_t step_delay = 40 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        int launch_col = rand() % s_led_cols;
        float hue = (float)(rand() % 360);
        /* 上升阶段 */
        uint8_t r = 0, g = 0, b = 0;
        for (int row = s_led_rows - 1; row >= 0; row--) {
            ble_srv_led_clear_buf();
            int idx = ble_srv_led_matrix_idx(row, launch_col);
            if (idx >= 0) {
                hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
                ble_srv_led_set_pixel(idx, r, g, b);
            }
            if (row < s_led_rows - 1) {
                int trail_idx = ble_srv_led_matrix_idx(row + 1, launch_col);
                if (trail_idx >= 0) {
                    ble_srv_led_set_pixel(trail_idx, (uint8_t)(r * 0.3f), (uint8_t)(g * 0.3f), (uint8_t)(b * 0.3f));
                }
            }
            ble_srv_led_send_frame();
            if (led_effect_wait_tick(step_delay)) return;
        }
        /* 爆炸阶段 */
        int explode_row = 0;
        int explode_col = launch_col;
        for (int frame = 0; frame < 15; frame++) {
            ble_srv_led_clear_buf();
            float radius = (float)frame * 0.8f;
            for (int row = 0; row < s_led_rows; row++) {
                for (int col = 0; col < s_led_cols; col++) {
                    float dx = (float)col - (float)explode_col;
                    float dy = (float)row - (float)explode_row;
                    float dist = sqrtf(dx * dx + dy * dy);
                    if (fabsf(dist - radius) < 1.0f) {
                        float fade = 1.0f - (float)frame / 15.0f;
                        uint8_t r, g, b;
                        hsv_to_rgb(hue + dist * 30.0f, 1.0f, fade, &r, &g, &b);
                        ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
                    }
                }
            }
            ble_srv_led_send_frame();
            if (led_effect_wait_tick(step_delay)) return;
        }
        ble_srv_led_clear_buf();
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay * 5)) return;
    }
}

/* 灯笼：红色灯笼柔和发光 */
static void effect_lantern(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 500.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        float brightness = 0.6f + 0.4f * sinf(phase);
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float cx = (float)s_led_cols / 2.0f;
                float dx = fabsf((float)col - cx);
                float center_fade = 1.0f - dx / (float)s_led_cols;
                if (center_fade < 0) center_fade = 0;
                uint8_t r = (uint8_t)(255 * brightness * center_fade);
                uint8_t g = (uint8_t)(50 * brightness * center_fade);
                uint8_t b = (uint8_t)(10 * brightness * center_fade);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 彩带：多条彩带飘动 */
static void effect_ribbon(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 200.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        ble_srv_led_clear_buf();
        int ribbons = 3;
        for (int r = 0; r < ribbons; r++) {
            float rphase = phase + (float)r * 2.0f;
            for (int col = 0; col < s_led_cols; col++) {
                int row = (int)((s_led_rows / 2) + sinf(rphase + (float)col * 0.3f) * (s_led_rows / 3));
                if (row >= 0 && row < s_led_rows) {
                    int idx = ble_srv_led_matrix_idx(row, col);
                    if (idx >= 0) {
                        uint8_t cr, cg, cb;
                        hsv_to_rgb((float)r * 120.0f + col * 10.0f, 1.0f, 1.0f, &cr, &cg, &cb);
                        ble_srv_led_set_pixel(idx, cr, cg, cb);
                    }
                }
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 干涉条纹：波纹叠加 */
static void effect_interference(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 300.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        float s1x = (float)s_led_cols * 0.3f;
        float s1y = (float)s_led_rows * 0.3f;
        float s2x = (float)s_led_cols * 0.7f;
        float s2y = (float)s_led_rows * 0.7f;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float d1 = sqrtf((float)col - s1x) * ((float)col - s1x) + ((float)row - s1y) * ((float)row - s1y);
                float d2 = sqrtf((float)col - s2x) * ((float)col - s2x) + ((float)row - s2y) * ((float)row - s2y);
                d1 = sqrtf(d1); d2 = sqrtf(d2);
                float wave = sinf(d1 * 2.0f - phase) + sinf(d2 * 2.0f - phase);
                wave = (wave + 2.0f) / 4.0f;
                uint8_t r, g, b;
                hsv_to_rgb(wave * 270.0f, 1.0f, wave, &r, &g, &b);
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 等高线：地形等高线图 */
static void effect_contour(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 500.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float x = (float)col / (float)s_led_cols * 4.0f;
                float y = (float)row / (float)s_led_rows * 4.0f;
                float h = sinf(x + phase) * cosf(y + phase * 0.7f) +
                          sinf(x * 2.0f - y + phase * 1.3f) * 0.5f;
                float contour = fmodf(h * 3.0f, 1.0f);
                if (contour < 0) contour += 1.0f;
                uint8_t r, g, b;
                if (contour < 0.1f) {
                    r = 255; g = 255; b = 255;
                } else {
                    hsv_to_rgb(h * 60.0f + 120.0f, 0.7f, 0.5f, &r, &g, &b);
                }
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 温度图：热成像图效果 */
static void effect_heatmap(uint8_t speed)
{
    float phase = 0.0f;
    float step = (float)speed / 400.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float x = (float)col / (float)s_led_cols;
                float y = (float)row / (float)s_led_rows;
                float v = sinf(phase + x * 3.0f) * 0.3f +
                          cosf(phase * 1.3f + y * 2.0f) * 0.3f +
                          sinf(phase * 0.7f + (x + y) * 4.0f) * 0.2f + 0.5f;
                if (v < 0) v = 0;
                if (v > 1) v = 1;
                uint8_t r, g, b;
                if (v < 0.3f) { r = 0; g = 0; b = (uint8_t)(v * 255 / 0.3f); }
                else if (v < 0.6f) { r = 0; g = (uint8_t)((v - 0.3f) * 255 / 0.3f); b = (uint8_t)(255 - (v - 0.3f) * 255 / 0.3f); }
                else if (v < 0.8f) { r = (uint8_t)((v - 0.6f) * 255 / 0.2f); g = 255; b = 0; }
                else { r = 255; g = (uint8_t)(255 - (v - 0.8f) * 255 / 0.2f); b = 0; }
                ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
            }
        }
        ble_srv_led_send_frame();
        phase += step;
    }
}

/* 细胞分裂：细胞分裂动画 */
static void effect_cell(uint8_t speed)
{
    uint32_t step_delay = 100 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    float phase = 0.0f;
    while (1) {
        ble_srv_led_clear_buf();
        float cx = (float)s_led_cols / 2.0f;
        float cy = (float)s_led_rows / 2.0f;
        float split = sinf(phase) * 0.5f + 0.5f;
        float r1 = 2.0f + split * 3.0f;
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                float dx1 = (float)col - (cx - split * 3.0f);
                float dy1 = (float)row - cy;
                float dx2 = (float)col - (cx + split * 3.0f);
                float dy2 = (float)row - cy;
                float d1 = sqrtf(dx1 * dx1 + dy1 * dy1);
                float d2 = sqrtf(dx2 * dx2 + dy2 * dy2);
                if (d1 < r1 || d2 < r1) {
                    uint8_t r = (uint8_t)(100 + split * 155);
                    uint8_t g = (uint8_t)(200 - split * 100);
                    uint8_t b = (uint8_t)(50 + split * 50);
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col), r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
        phase += 0.05f;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* DNA双螺旋：DNA旋转动画 */
static void effect_dna(uint8_t speed)
{
    float angle = 0.0f;
    float step = (float)speed / 100.0f;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        if (led_effect_wait_tick(step_delay)) return;
        ble_srv_led_clear_buf();
        int cx = s_led_cols / 2;
        for (int row = 0; row < s_led_rows; row++) {
            float phase = angle + (float)row * 0.5f;
            int x1 = cx + (int)(sinf(phase) * (float)cx * 0.6f);
            int x2 = cx + (int)(sinf(phase + 3.14159265f) * (float)cx * 0.6f);
            if (x1 >= 0 && x1 < s_led_cols) {
                int idx = ble_srv_led_matrix_idx(row, x1);
                if (idx >= 0) ble_srv_led_set_pixel(idx, 0, 200, 255);
            }
            if (x2 >= 0 && x2 < s_led_cols) {
                int idx = ble_srv_led_matrix_idx(row, x2);
                if (idx >= 0) ble_srv_led_set_pixel(idx, 255, 100, 100);
            }
            /* 连接线 */
            if ((row + (int)(angle * 2)) % 3 == 0 && x1 != x2) {
                int mid = (x1 + x2) / 2;
                int idx = ble_srv_led_matrix_idx(row, mid);
                if (idx >= 0) ble_srv_led_set_pixel(idx, 100, 100, 100);
            }
        }
        ble_srv_led_send_frame();
        angle += step;
    }
}

/* 矩阵代码雨：黑客帝国代码雨 */
static void effect_matrix_rain(uint8_t speed)
{
    int *drops = (int *)malloc((size_t)s_led_cols * sizeof(int));
    uint8_t *trail = (uint8_t *)calloc((size_t)s_led_count, 1);
    if (!drops || !trail) { free(drops); free(trail); return; }
    for (int i = 0; i < s_led_cols; i++) {
        drops[i] = rand() % s_led_rows - s_led_rows;
    }
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        /* 衰减 */
        for (int i = 0; i < s_led_count; i++) {
            if (trail[i] > 20) trail[i] -= 20;
            else trail[i] = 0;
        }
        /* 更新雨滴 */
        for (int col = 0; col < s_led_cols; col++) {
            drops[col]++;
            if (drops[col] >= 0 && drops[col] < s_led_rows) {
                int idx = ble_srv_led_matrix_idx(drops[col], col);
                if (idx >= 0) trail[idx] = 255;
            }
            if (drops[col] >= s_led_rows + 3) {
                drops[col] = -rand() % s_led_rows;
            }
        }
        /* 渲染 */
        ble_srv_led_clear_buf();
        for (int i = 0; i < s_led_count; i++) {
            if (trail[i] > 0) {
                ble_srv_led_set_pixel(i, 0, trail[i], (uint8_t)(trail[i] * 0.3f));
            }
        }
        ble_srv_led_send_frame();
        if (led_effect_wait_tick(step_delay)) { free(drops); free(trail); return; }
    }
}

/* 像素粒子：粒子系统 */
static void effect_particle(uint8_t speed)
{
    int max_p = 15;
    struct { float x, y, vx, vy; uint8_t life; float hue; } *p;
    p = malloc((size_t)max_p * sizeof(*p));
    if (!p) return;
    for (int i = 0; i < max_p; i++) p[i].life = 0;
    uint32_t step_delay = LED_EFFECT_TICK_MS;
    int spawn_timer = 0;
    while (1) {
        if (led_effect_wait_tick(step_delay)) { free(p); return; }
        ble_srv_led_clear_buf();
        spawn_timer++;
        if (spawn_timer >= 3) {
            spawn_timer = 0;
            for (int i = 0; i < max_p; i++) {
                if (p[i].life == 0) {
                    p[i].x = (float)(rand() % s_led_cols);
                    p[i].y = (float)(rand() % s_led_rows);
                    p[i].vx = ((float)(rand() % 100) - 50) / 200.0f;
                    p[i].vy = ((float)(rand() % 100) - 50) / 200.0f;
                    p[i].life = (uint8_t)(20 + rand() % 30);
                    p[i].hue = (float)(rand() % 360);
                    break;
                }
            }
        }
        for (int i = 0; i < max_p; i++) {
            if (p[i].life > 0) {
                p[i].x += p[i].vx;
                p[i].y += p[i].vy;
                p[i].life--;
                int idx = ble_srv_led_matrix_idx((int)p[i].y, (int)p[i].x);
                if (idx >= 0) {
                    float fade = (float)p[i].life / 50.0f;
                    if (fade > 1) fade = 1;
                    uint8_t r, g, b;
                    hsv_to_rgb(p[i].hue, 1.0f, fade, &r, &g, &b);
                    ble_srv_led_set_pixel(idx, r, g, b);
                }
            }
        }
        ble_srv_led_send_frame();
    }
}

/* 光剑：光剑展开收回 */
static void effect_lightsaber(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 30 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int len = 0;
    int dir = 1;
    while (1) {
        ble_srv_led_clear_buf();
        for (int i = 0; i < len && i < s_led_count; i++) {
            float fade = 1.0f - (float)i / (float)s_led_count * 0.3f;
            ble_srv_led_set_pixel(i, (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
        }
        /* 剑柄 */
        if (len < s_led_count) {
            ble_srv_led_set_pixel(len, 100, 100, 100);
        }
        ble_srv_led_send_frame();
        len += dir;
        if (len >= s_led_count) { len = s_led_count; dir = -1; }
        else if (len <= 0) { len = 0; dir = 1; }
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 俄罗斯方块：方块下落堆积 */
static void effect_tetris(uint8_t speed)
{
    uint32_t step_delay = 200 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    uint8_t *grid = (uint8_t *)calloc((size_t)s_led_count, 1);
    if (!grid) return;
    while (1) {
        /* 生成新方块 */
        int col = rand() % s_led_cols;
        float hue = (float)(rand() % 360);
        int row = 0;
        while (1) {
            ble_srv_led_clear_buf();
            /* 绘制堆积层 */
            for (int r = 0; r < s_led_rows; r++) {
                for (int c = 0; c < s_led_cols; c++) {
                    int idx = ble_srv_led_matrix_idx(r, c);
                    if (idx >= 0 && grid[idx]) {
                        uint8_t rr, gg, bb;
                        hsv_to_rgb(grid[idx] * 30.0f, 1.0f, 1.0f, &rr, &gg, &bb);
                        ble_srv_led_set_pixel(idx, rr, gg, bb);
                    }
                }
            }
            /* 绘制下落方块 */
            int fidx = ble_srv_led_matrix_idx(row, col);
            if (fidx >= 0) {
                uint8_t rr, gg, bb;
                hsv_to_rgb(hue, 1.0f, 1.0f, &rr, &gg, &bb);
                ble_srv_led_set_pixel(fidx, rr, gg, bb);
            }
            ble_srv_led_send_frame();
            if (led_effect_wait_tick(step_delay)) { free(grid); return; }
            row++;
            if (row >= s_led_rows) {
                row = s_led_rows - 1;
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx >= 0) grid[idx] = (uint8_t)(hue / 30.0f) + 1;
                break;
            }
            int below_idx = ble_srv_led_matrix_idx(row, col);
            if (below_idx >= 0 && grid[below_idx]) {
                int idx = ble_srv_led_matrix_idx(row - 1, col);
                if (idx >= 0) grid[idx] = (uint8_t)(hue / 30.0f) + 1;
                break;
            }
        }
        /* 检查是否堆满 */
        int filled = 0;
        for (int i = 0; i < s_led_count; i++) if (grid[i]) filled++;
        if (filled > s_led_count * 3 / 4) {
            memset(grid, 0, (size_t)s_led_count);
        }
    }
}

/* 棋盘扫描：对角线顺序点亮 */
static void effect_chess_scan(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 30 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    int diag = 0;
    int max_diag = s_led_rows + s_led_cols - 1;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            for (int col = 0; col < s_led_cols; col++) {
                int d = row + col;
                if (d <= diag) {
                    float fade = 1.0f - (float)(diag - d) / (float)max_diag;
                    if (fade < 0.2f) fade = 0.2f;
                    ble_srv_led_set_pixel(ble_srv_led_matrix_idx(row, col),
                                          (uint8_t)(r * fade), (uint8_t)(g * fade), (uint8_t)(b * fade));
                }
            }
        }
        ble_srv_led_send_frame();
        diag++;
        if (diag > max_diag + 5) diag = 0;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 彩虹瀑布：彩虹色从顶部流到底部 */
static void effect_rainbow_fall(uint8_t speed)
{
    float hue = 0.0f;
    float step = (float)speed / 30.0f;
    uint32_t step_delay = 80 / (speed > 0 ? speed : 1);
    if (step_delay < LED_EFFECT_TICK_MS) step_delay = LED_EFFECT_TICK_MS;
    while (1) {
        ble_srv_led_clear_buf();
        for (int row = 0; row < s_led_rows; row++) {
            uint8_t r, g, b;
            hsv_to_rgb(fmodf(hue + (float)row * 30.0f, 360.0f), 1.0f, 1.0f, &r, &g, &b);
            for (int col = 0; col < s_led_cols; col++) {
                int idx = ble_srv_led_matrix_idx(row, col);
                if (idx >= 0) ble_srv_led_set_pixel(idx, r, g, b);
            }
        }
        ble_srv_led_send_frame();
        hue += step;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* 时钟：显示当前时间 HH:MM（需要至少 17x5 矩阵） */
static void effect_clock(uint8_t r, uint8_t g, uint8_t b, uint8_t speed)
{
    uint32_t step_delay = 1000 / (speed > 0 ? speed : 1);
    if (step_delay < 200) step_delay = 200;
    bool colon_on = true;
    while (1) {
        ble_srv_led_clear_buf();
        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        int hh = t->tm_hour;
        int mm = t->tm_min;
        int h1 = hh / 10, h2 = hh % 10;
        int m1 = mm / 10, m2 = mm % 10;

        int start_col = (s_led_cols - 17) / 2;  /* 居中：4*3+1+4间隔=17 */
        if (start_col < 0) start_col = 0;

        draw_digit(h1, start_col, r, g, b);
        draw_digit(h2, start_col + 4, r, g, b);
        if (colon_on) {
            draw_digit(10, start_col + 8, r, g, b);  /* 冒号 */
        }
        draw_digit(m1, start_col + 10, r, g, b);
        draw_digit(m2, start_col + 14, r, g, b);

        ble_srv_led_send_frame();
        colon_on = !colon_on;
        if (led_effect_wait_tick(step_delay)) return;
    }
}

/* ====================== Effect 任务与控制 ====================== */

static void ble_srv_led_effect_task(void *arg)
{
    (void)arg;
    TaskHandle_t self_handle = xTaskGetCurrentTaskHandle();
    uint8_t r = 0, g = 0, b = 0, speed = LED_DEFAULT_SPEED;
    ble_led_effect_t effect = BLE_LED_EFFECT_NONE;
    ble_led_effect_t last_effect = BLE_LED_EFFECT_NONE;

    int lock_fail_count = 0;
    while (1) {
        bool should_exit = false;
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) == pdTRUE) {
            lock_fail_count = 0;
            if (s_effect_task != self_handle) {
                should_exit = true;
            }
            if (!should_exit) {
                r = s_red;
                g = s_green;
                b = s_blue;
                speed = s_speed;
                effect = s_effect;
            }
            xSemaphoreGive(s_lock);
        } else {
            lock_fail_count++;
            BLE_SRV_LOGW(TAG, "Failed to take LED lock in effect task (count=%d)", lock_fail_count);
            if (lock_fail_count > 10) {
                BLE_SRV_LOGE(TAG, "LED lock failed too many times, exiting effect task");
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(LED_LOCK_RETRY_MS));
            continue;
        }

        if (should_exit) {
            break;
        }

        if (effect != last_effect) {
            BLE_SRV_LOGI(TAG, "Effect running: effect=%d, speed=%d, leds=%d (%dx%d)",
                         effect, speed, s_led_count, s_led_rows, s_led_cols);
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
        case BLE_LED_EFFECT_CHASE:
            effect_chase(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_COLOR_WIPE:
            effect_color_wipe(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_THEATER_CHASE:
            effect_theater_chase(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_WAVE:
            effect_wave(speed);
            break;
        case BLE_LED_EFFECT_METEOR:
            effect_meteor(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_FIRE:
            effect_fire(speed);
            break;
        case BLE_LED_EFFECT_SCAN:
            effect_scan(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_MARQUEE:
            effect_marquee(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_DUAL_CHASE:
            effect_dual_chase(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_PIXEL_RAIN:
            effect_pixel_rain(speed);
            break;
        case BLE_LED_EFFECT_RANDOM_BLINK:
            effect_random_blink(speed);
            break;
        case BLE_LED_EFFECT_GRADIENT_FALL:
            effect_gradient_fall(speed);
            break;
        case BLE_LED_EFFECT_HEART:
            effect_heart(speed);
            break;
        case BLE_LED_EFFECT_CHESSBOARD:
            effect_chessboard(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_RING:
            effect_ring(speed);
            break;
        case BLE_LED_EFFECT_LIGHTNING:
            effect_lightning(speed);
            break;
        case BLE_LED_EFFECT_EXPLOSION:
            effect_explosion(speed);
            break;
        case BLE_LED_EFFECT_SNOW:
            effect_snow(speed);
            break;
        case BLE_LED_EFFECT_LASER_SCAN:
            effect_laser_scan(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_WATERFLOW:
            effect_waterflow(speed);
            break;
        case BLE_LED_EFFECT_STAR_BLINK:
            effect_star_blink(speed);
            break;
        case BLE_LED_EFFECT_AURORA:
            effect_aurora(speed);
            break;
        case BLE_LED_EFFECT_ROTATE_RAINBOW:
            effect_rotate_rainbow(speed);
            break;
        case BLE_LED_EFFECT_RIPPLE:
            effect_ripple(speed);
            break;
        case BLE_LED_EFFECT_MOSAIC:
            effect_mosaic(speed);
            break;
        case BLE_LED_EFFECT_AUDIO_RHYTHM:
            effect_audio_rhythm(speed);
            break;
        /* === 矩阵扩展效果 === */
        case BLE_LED_EFFECT_BLOCK_BOUNCE:
            effect_block_bounce(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_CROSS:
            effect_cross(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_SPIRAL:
            effect_spiral(speed);
            break;
        case BLE_LED_EFFECT_DIAMOND:
            effect_diamond(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_TRIANGLE:
            effect_triangle(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_CIRCLE_PULSE:
            effect_circle_pulse(speed);
            break;
        case BLE_LED_EFFECT_BALL:
            effect_ball(speed);
            break;
        case BLE_LED_EFFECT_SNAKE:
            effect_snake(speed);
            break;
        case BLE_LED_EFFECT_LIFE:
            effect_life(speed);
            break;
        case BLE_LED_EFFECT_SCROLL_TEXT:
            effect_scroll_text(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_SMILEY:
            effect_smiley(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_OCEAN:
            effect_ocean(speed);
            break;
        case BLE_LED_EFFECT_FIREFLY:
            effect_firefly(speed);
            break;
        case BLE_LED_EFFECT_LAVA:
            effect_lava(speed);
            break;
        case BLE_LED_EFFECT_CLOUD:
            effect_cloud(speed);
            break;
        case BLE_LED_EFFECT_SUNRISE:
            effect_sunrise(speed);
            break;
        case BLE_LED_EFFECT_STARRY:
            effect_starry(speed);
            break;
        case BLE_LED_EFFECT_FRACTAL:
            effect_fractal(speed);
            break;
        case BLE_LED_EFFECT_KALEIDOSCOPE:
            effect_kaleidoscope(speed);
            break;
        case BLE_LED_EFFECT_HOLOGRAM:
            effect_hologram(speed);
            break;
        case BLE_LED_EFFECT_GRADIENT_FULL:
            effect_gradient_full(speed);
            break;
        case BLE_LED_EFFECT_DIAGONAL_SCAN:
            effect_diagonal_scan(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_GRID_PULSE:
            effect_grid_pulse(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_PUZZLE:
            effect_puzzle(speed);
            break;
        case BLE_LED_EFFECT_TREE:
            effect_tree(speed);
            break;
        case BLE_LED_EFFECT_FIREWORK:
            effect_firework(speed);
            break;
        case BLE_LED_EFFECT_LANTERN:
            effect_lantern(speed);
            break;
        case BLE_LED_EFFECT_RIBBON:
            effect_ribbon(speed);
            break;
        case BLE_LED_EFFECT_INTERFERENCE:
            effect_interference(speed);
            break;
        case BLE_LED_EFFECT_CONTOUR:
            effect_contour(speed);
            break;
        case BLE_LED_EFFECT_HEATMAP:
            effect_heatmap(speed);
            break;
        case BLE_LED_EFFECT_CELL:
            effect_cell(speed);
            break;
        case BLE_LED_EFFECT_DNA:
            effect_dna(speed);
            break;
        case BLE_LED_EFFECT_MATRIX_RAIN:
            effect_matrix_rain(speed);
            break;
        case BLE_LED_EFFECT_PARTICLE:
            effect_particle(speed);
            break;
        case BLE_LED_EFFECT_LIGHTSABER:
            effect_lightsaber(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_TETRIS:
            effect_tetris(speed);
            break;
        case BLE_LED_EFFECT_CHESS_SCAN:
            effect_chess_scan(r, g, b, speed);
            break;
        case BLE_LED_EFFECT_RAINBOW_FALL:
            effect_rainbow_fall(speed);
            break;
        case BLE_LED_EFFECT_CLOCK:
            effect_clock(r, g, b, speed);
            break;
        default:
            break;
        }
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) == pdTRUE) {
        if (s_effect_task == self_handle) {
            s_effect_task = NULL;
        }
        xSemaphoreGive(s_lock);
    }

    s_effect_task_done = true;
    vTaskDelete(NULL);
}

static void ble_srv_led_start_effect(void)
{
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGW(TAG, "Failed to take LED lock in start_effect");
        return;
    }

    if (s_effect_task) {
        TaskHandle_t task = s_effect_task;
        xSemaphoreGive(s_lock);
        xTaskNotify(task, LED_EFFECT_NOTIFY_RESTART, eSetBits);
        return;
    }

    BaseType_t ret = xTaskCreate(ble_srv_led_effect_task, "led_eff", LED_TASK_STACK, NULL, LED_TASK_PRIO, &s_effect_task);
    if (ret == pdPASS) {
        s_effect_task_done = false;
    }
    if (ret != pdPASS) {
        BLE_SRV_LOGE(TAG, "Failed to create effect task");
        s_effect_task = NULL;
    }

    xSemaphoreGive(s_lock);
}

static void ble_srv_led_stop_effect(uint32_t wait_ms)
{
    TaskHandle_t task = NULL;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGW(TAG, "Failed to take LED lock in stop_effect");
        return;
    }

    if (!s_effect_task) {
        xSemaphoreGive(s_lock);
        return;
    }

    task = s_effect_task;
    s_effect_task = NULL;

    xSemaphoreGive(s_lock);

    xTaskNotify(task, LED_EFFECT_NOTIFY_STOP, eSetBits);

    if (wait_ms > 0) {
        uint32_t waited = 0;
        while (waited < wait_ms) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
            if (s_effect_task_done) {
                break;
            }
        }
    }
}

/* ====================== 公共 API ====================== */

bool ble_srv_led_init(void)
{
    if (s_initialized) {
        BLE_SRV_LOGW(TAG, "LED already initialized");
        return true;
    }

    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        BLE_SRV_LOGE(TAG, "Failed to create LED lock");
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
        BLE_SRV_LOGE(TAG, "Failed to create RMT TX channel: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    rmt_copy_encoder_config_t copy_encoder_cfg = {};
    ret = rmt_new_copy_encoder(&copy_encoder_cfg, &s_copy_encoder);
    if (ret != ESP_OK) {
        BLE_SRV_LOGE(TAG, "Failed to create copy encoder: %s", esp_err_to_name(ret));
        rmt_del_channel(s_led_chan);
        s_led_chan = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    ret = rmt_enable(s_led_chan);
    if (ret != ESP_OK) {
        BLE_SRV_LOGE(TAG, "Failed to enable RMT channel: %s", esp_err_to_name(ret));
        rmt_del_encoder(s_copy_encoder);
        rmt_del_channel(s_led_chan);
        s_copy_encoder = NULL;
        s_led_chan = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    /* 预分配帧缓冲和 RMT 符号缓冲 */
    s_pixel_buf_size = (size_t)LED_COUNT_MAX * 3;
    s_pixel_buf = (uint8_t *)heap_caps_malloc(s_pixel_buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_pixel_buf) {
        s_pixel_buf = (uint8_t *)heap_caps_malloc(s_pixel_buf_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_pixel_buf) {
        BLE_SRV_LOGE(TAG, "Failed to allocate pixel buffer (%u bytes)", (unsigned)s_pixel_buf_size);
        rmt_del_encoder(s_copy_encoder);
        rmt_disable(s_led_chan);
        rmt_del_channel(s_led_chan);
        s_copy_encoder = NULL;
        s_led_chan = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    s_symbols_count = (size_t)LED_COUNT_MAX * 24 + 1;
    s_symbols = (rmt_symbol_word_t *)heap_caps_malloc(s_symbols_count * sizeof(rmt_symbol_word_t),
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_symbols) {
        s_symbols = (rmt_symbol_word_t *)heap_caps_malloc(s_symbols_count * sizeof(rmt_symbol_word_t),
                                                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!s_symbols) {
        BLE_SRV_LOGE(TAG, "Failed to allocate symbol buffer (%u bytes)",
                     (unsigned)(s_symbols_count * sizeof(rmt_symbol_word_t)));
        heap_caps_free(s_pixel_buf);
        s_pixel_buf = NULL;
        rmt_del_encoder(s_copy_encoder);
        rmt_disable(s_led_chan);
        rmt_del_channel(s_led_chan);
        s_copy_encoder = NULL;
        s_led_chan = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    s_led_rows = CONFIG_BLE_SRV_LED_ROWS;
    s_led_cols = CONFIG_BLE_SRV_LED_COLS;
    s_led_count = (int)s_led_rows * (int)s_led_cols;
    if (s_led_count < 1) s_led_count = 1;
    if (s_led_count > LED_COUNT_MAX) s_led_count = LED_COUNT_MAX;

    ble_srv_led_clear_buf();
    ble_srv_led_send_frame();

    s_initialized = true;
    s_led_on = false;
    s_red = 0;
    s_green = 0;
    s_blue = 0;
    s_effect = BLE_LED_EFFECT_NONE;
    s_effect_task = NULL;

    BLE_SRV_LOGI(TAG, "WS2812 LED initialized on GPIO %d, layout=%dx%d (count=%d, max=%d)",
                 BLE_LED_GPIO, s_led_rows, s_led_cols, s_led_count, LED_COUNT_MAX);
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

    ble_srv_led_clear_buf();
    ble_srv_led_send_frame();

    if (s_pixel_buf) {
        heap_caps_free(s_pixel_buf);
        s_pixel_buf = NULL;
    }
    if (s_symbols) {
        heap_caps_free(s_symbols);
        s_symbols = NULL;
    }
    s_pixel_buf_size = 0;
    s_symbols_count = 0;

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

    BLE_SRV_LOGI(TAG, "LED deinitialized");
}

bool ble_srv_led_set_on(bool on)
{
    if (!s_initialized) {
        BLE_SRV_LOGE(TAG, "LED not initialized");
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGE(TAG, "Failed to take LED lock in set_on");
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
            ble_srv_led_fill_all(r, g, b);
            ble_srv_led_send_frame();
        }
    } else {
        xSemaphoreGive(s_lock);
        ble_srv_led_stop_effect(LED_STOP_EFFECT_WAIT_MS);
        ble_srv_led_clear_buf();
        ble_srv_led_send_frame();
    }

    BLE_SRV_LOGI(TAG, "LED %s", on ? "ON" : "OFF");
    return true;
}

bool ble_srv_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue)
{
    if (!s_initialized) {
        BLE_SRV_LOGE(TAG, "LED not initialized");
        return false;
    }

    bool send_now = false;
    uint8_t r, g, b;

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGE(TAG, "Failed to take LED lock in set_rgb");
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

    TaskHandle_t effect_task = s_effect_task;
    bool should_notify = s_led_on && s_effect != BLE_LED_EFFECT_NONE && effect_task != NULL;

    xSemaphoreGive(s_lock);

    if (send_now) {
        ble_srv_led_fill_all(r, g, b);
        ble_srv_led_send_frame();
    }

    if (should_notify && effect_task) {
        xTaskNotify(effect_task, LED_EFFECT_NOTIFY_RESTART, eSetBits);
    }

    BLE_SRV_LOGI(TAG, "LED RGB set: R=%d, G=%d, B=%d", red, green, blue);
    return true;
}

bool ble_srv_led_set_effect(ble_led_effect_t effect, uint8_t speed)
{
    if (!s_initialized) {
        BLE_SRV_LOGE(TAG, "LED not initialized");
        return false;
    }

    if (effect >= BLE_LED_EFFECT_MAX) {
        BLE_SRV_LOGW(TAG, "Invalid LED effect: %d", effect);
        return false;
    }

    if (speed == 0) {
        speed = 1;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGE(TAG, "Failed to take LED lock in set_effect");
        return false;
    }

    ble_led_effect_t old_effect = s_effect;
    s_effect = effect;
    s_speed = speed;
    bool led_on = s_led_on;
    uint8_t r = s_red, g = s_green, b = s_blue;
    TaskHandle_t effect_task = s_effect_task;

    xSemaphoreGive(s_lock);

    if (led_on && effect != BLE_LED_EFFECT_NONE) {
        if (old_effect == BLE_LED_EFFECT_NONE) {
            ble_srv_led_start_effect();
        } else if (effect_task) {
            xTaskNotify(effect_task, LED_EFFECT_NOTIFY_RESTART, eSetBits);
        }
    } else if (led_on) {
        ble_srv_led_stop_effect(LED_STOP_EFFECT_WAIT_MS);
        ble_srv_led_fill_all(r, g, b);
        ble_srv_led_send_frame();
    } else {
        ble_srv_led_stop_effect(LED_STOP_EFFECT_WAIT_MS);
    }

    BLE_SRV_LOGI(TAG, "LED effect set: %d, speed=%d", effect, speed);
    return true;
}

bool ble_srv_led_set_layout(uint8_t rows, uint8_t cols)
{
    if (!s_initialized) {
        BLE_SRV_LOGE(TAG, "LED not initialized");
        return false;
    }

    if (rows == 0 || cols == 0) {
        BLE_SRV_LOGW(TAG, "Invalid layout: %dx%d", rows, cols);
        return false;
    }

    int new_count = (int)rows * (int)cols;
    if (new_count > LED_COUNT_MAX) {
        BLE_SRV_LOGW(TAG, "Layout %dx%d=%d exceeds max %d, rejected",
                     rows, cols, new_count, LED_COUNT_MAX);
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGE(TAG, "Failed to take LED lock in set_layout");
        return false;
    }

    bool was_on = s_led_on;
    ble_led_effect_t cur_effect = s_effect;
    uint8_t r = s_red, g = s_green, b = s_blue;

    /* 先停止当前效果，避免 effect 任务读取旧布局 */
    if (s_effect_task && cur_effect != BLE_LED_EFFECT_NONE) {
        TaskHandle_t task = s_effect_task;
        s_effect_task = NULL;
        xSemaphoreGive(s_lock);
        xTaskNotify(task, LED_EFFECT_NOTIFY_STOP, eSetBits);
        uint32_t waited = 0;
        while (waited < LED_STOP_EFFECT_WAIT_MS) {
            vTaskDelay(pdMS_TO_TICKS(10));
            waited += 10;
            if (s_effect_task_done) break;
        }
        if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
            BLE_SRV_LOGE(TAG, "Failed to re-acquire LED lock in set_layout");
            return false;
        }
    }

    s_led_rows = rows;
    s_led_cols = cols;
    s_led_count = new_count;

    ble_srv_led_clear_buf();
    if (was_on) {
        if (cur_effect != BLE_LED_EFFECT_NONE) {
            /* 矩阵尺寸改变后重新启动效果 */
            xSemaphoreGive(s_lock);
            ble_srv_led_start_effect();
        } else {
            uint8_t lr = r, lg = g, lb = b;
            xSemaphoreGive(s_lock);
            ble_srv_led_fill_all(lr, lg, lb);
            ble_srv_led_send_frame();
        }
    } else {
        xSemaphoreGive(s_lock);
        ble_srv_led_send_frame();
    }

    BLE_SRV_LOGI(TAG, "LED layout set: %dx%d (count=%d)", rows, cols, new_count);
    return true;
}

bool ble_srv_led_get_status(ble_led_status_t *status)
{
    if (!status || !s_initialized) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGE(TAG, "Failed to take LED lock in get_status");
        return false;
    }

    status->on = s_led_on ? 1 : 0;
    status->effect = (uint8_t)s_effect;
    status->speed = s_speed;
    status->red = s_red;
    status->green = s_green;
    status->blue = s_blue;
    status->rows = s_led_rows;
    status->cols = s_led_cols;

    xSemaphoreGive(s_lock);
    return true;
}

bool ble_srv_led_get_layout(ble_led_layout_t *layout)
{
    if (!layout || !s_initialized) {
        return false;
    }

    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(LED_LOCK_TIMEOUT_MS)) != pdTRUE) {
        BLE_SRV_LOGE(TAG, "Failed to take LED lock in get_layout");
        return false;
    }

    layout->rows = s_led_rows;
    layout->cols = s_led_cols;

    xSemaphoreGive(s_lock);
    return true;
}
