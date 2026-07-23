#ifndef BLE_SRV_LED_H
#define BLE_SRV_LED_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LED_SVC_UUID              0xFFB0
#define BLE_LED_CTRL_CHAR_UUID        0xFFB1
#define BLE_LED_COLOR_CHAR_UUID       0xFFB2
#define BLE_LED_EFFECT_CHAR_UUID      0xFFB3
#define BLE_LED_LAYOUT_CHAR_UUID      0xFFB4

typedef enum {
    BLE_LED_CTRL_OFF = 0x00,
    BLE_LED_CTRL_ON  = 0x01,
} ble_led_ctrl_t;

typedef enum {
    BLE_LED_EFFECT_NONE          = 0x00,
    BLE_LED_EFFECT_BREATH        = 0x01,
    BLE_LED_EFFECT_BLINK         = 0x02,
    BLE_LED_EFFECT_RAINBOW       = 0x03,
    BLE_LED_EFFECT_STROBE        = 0x04,
    BLE_LED_EFFECT_CHASE         = 0x05,  /* 流光追逐：彗星状光点沿灯带移动 */
    BLE_LED_EFFECT_COLOR_WIPE    = 0x06,  /* 色彩擦除：逐颗填充再逐颗熄灭 */
    BLE_LED_EFFECT_THEATER_CHASE = 0x07,  /* 剧场追逐：间隔点亮的经典追逐 */
    BLE_LED_EFFECT_WAVE          = 0x08,  /* 波浪：按位置正弦调制的亮度波动 */
    BLE_LED_EFFECT_METEOR        = 0x09,  /* 流星：带渐变拖尾的流星 */
    BLE_LED_EFFECT_FIRE          = 0x0A,  /* 火焰：基于红黄的火焰闪烁 */
    BLE_LED_EFFECT_SCAN          = 0x0B,  /* 扫描：在矩阵上往返扫描一条亮线 */
    BLE_LED_EFFECT_MARQUEE       = 0x0C,  /* 跑马灯：多颗LED组成光点组移动 */
    BLE_LED_EFFECT_DUAL_CHASE    = 0x0D,  /* 双色追逐：两种颜色交替追逐 */
    BLE_LED_EFFECT_PIXEL_RAIN    = 0x0E,  /* 像素雨：模拟下雨，LED从上往下掉落 */
    BLE_LED_EFFECT_RANDOM_BLINK  = 0x0F,  /* 随机闪烁：随机位置随机颜色闪烁 */
    BLE_LED_EFFECT_GRADIENT_FALL = 0x10,  /* 渐变瀑布：颜色沿矩阵从上到下渐变流动 */
    BLE_LED_EFFECT_HEART         = 0x11,  /* 心形：在矩阵上显示跳动的心形图案 */
    BLE_LED_EFFECT_CHESSBOARD    = 0x12,  /* 棋盘格：棋盘格图案交替闪烁 */
    BLE_LED_EFFECT_RING          = 0x13,  /* 光环：从中心向外扩散的光环效果 */
    BLE_LED_EFFECT_LIGHTNING     = 0x14,  /* 闪电：随机位置的闪电闪烁效果 */
    BLE_LED_EFFECT_EXPLOSION     = 0x15,  /* 爆炸：从中心向外扩散的爆炸效果 */
    BLE_LED_EFFECT_SNOW          = 0x16,  /* 雪花：模拟雪花飘落效果 */
    BLE_LED_EFFECT_LASER_SCAN    = 0x17,  /* 激光扫描：激光束往返扫描，带尾迹效果 */
    BLE_LED_EFFECT_WATERFLOW     = 0x18,  /* 水流：水流沿灯带流动效果 */
    BLE_LED_EFFECT_STAR_BLINK    = 0x19,  /* 星星闪烁：模拟星星闪烁，随机亮度 */
    BLE_LED_EFFECT_AURORA        = 0x1A,  /* 极光：柔和的极光波动效果 */
    BLE_LED_EFFECT_ROTATE_RAINBOW = 0x1B, /* 旋转彩虹：彩虹色轮在矩阵上旋转 */
    BLE_LED_EFFECT_RIPPLE        = 0x1C,  /* 涟漪：像水滴落入水中的涟漪扩散 */
    BLE_LED_EFFECT_MOSAIC        = 0x1D,  /* 马赛克：随机大小的色块闪烁 */
    BLE_LED_EFFECT_AUDIO_RHYTHM  = 0x1E,  /* 音频律动：根据速度参数模拟音乐节奏闪烁 */
    /* === 矩阵扩展效果 === */
    BLE_LED_EFFECT_BLOCK_BOUNCE  = 0x1F,  /* 方块：方块在矩阵上弹跳 */
    BLE_LED_EFFECT_CROSS         = 0x20,  /* 十字架：十字形图案随机闪现 */
    BLE_LED_EFFECT_SPIRAL        = 0x21,  /* 螺旋：从中心向外螺旋扩散 */
    BLE_LED_EFFECT_DIAMOND       = 0x22,  /* 菱形：菱形从小到大再消失 */
    BLE_LED_EFFECT_TRIANGLE      = 0x23,  /* 三角形：三角形图案扫描 */
    BLE_LED_EFFECT_CIRCLE_PULSE  = 0x24,  /* 圆形脉冲：同心圆从中心扩散 */
    BLE_LED_EFFECT_BALL          = 0x25,  /* 弹球：球在矩阵内弹跳变色 */
    BLE_LED_EFFECT_SNAKE         = 0x26,  /* 贪吃蛇：经典贪吃蛇动画 */
    BLE_LED_EFFECT_LIFE          = 0x27,  /* 生命游戏：康威生命游戏 */
    BLE_LED_EFFECT_SCROLL_TEXT   = 0x28,  /* 字幕滚动：文字从右向左滚动 */
    BLE_LED_EFFECT_SMILEY        = 0x29,  /* 笑脸：笑脸图案眨眼 */
    BLE_LED_EFFECT_OCEAN         = 0x2A,  /* 海洋波浪：多层海浪叠加 */
    BLE_LED_EFFECT_FIREFLY       = 0x2B,  /* 萤火虫：萤火虫随机飘动发光 */
    BLE_LED_EFFECT_LAVA          = 0x2C,  /* 岩浆：熔岩流动效果 */
    BLE_LED_EFFECT_CLOUD         = 0x2D,  /* 云雾：柔和云雾飘动 */
    BLE_LED_EFFECT_SUNRISE       = 0x2E,  /* 日出日落：太阳升降 */
    BLE_LED_EFFECT_STARRY        = 0x2F,  /* 星空：夜空星星闪烁 */
    BLE_LED_EFFECT_FRACTAL       = 0x30,  /* 分形：朱利亚集分形 */
    BLE_LED_EFFECT_KALEIDOSCOPE  = 0x31,  /* 万花筒：对称万花筒图案 */
    BLE_LED_EFFECT_HOLOGRAM      = 0x32,  /* 全息图：全息投影彩虹波纹 */
    BLE_LED_EFFECT_GRADIENT_FULL = 0x33,  /* 全屏渐变：整屏颜色渐变流动 */
    BLE_LED_EFFECT_DIAGONAL_SCAN = 0x34,  /* 对角线扫描：对角线扫描矩阵 */
    BLE_LED_EFFECT_GRID_PULSE    = 0x35,  /* 网格脉冲：网格线从中心扩散 */
    BLE_LED_EFFECT_PUZZLE        = 0x36,  /* 色块拼图：随机色块变换 */
    BLE_LED_EFFECT_TREE          = 0x37,  /* 圣诞树：圣诞树彩灯闪烁 */
    BLE_LED_EFFECT_FIREWORK      = 0x38,  /* 烟花：烟花升空爆炸 */
    BLE_LED_EFFECT_LANTERN       = 0x39,  /* 灯笼：红色灯笼柔和发光 */
    BLE_LED_EFFECT_RIBBON        = 0x3A,  /* 彩带：多条彩带飘动 */
    BLE_LED_EFFECT_INTERFERENCE  = 0x3B,  /* 干涉条纹：波纹叠加 */
    BLE_LED_EFFECT_CONTOUR       = 0x3C,  /* 等高线：地形等高线图 */
    BLE_LED_EFFECT_HEATMAP       = 0x3D,  /* 温度图：热成像图效果 */
    BLE_LED_EFFECT_CELL          = 0x3E,  /* 细胞分裂：细胞分裂动画 */
    BLE_LED_EFFECT_DNA           = 0x3F,  /* DNA双螺旋：DNA旋转动画 */
    BLE_LED_EFFECT_MATRIX_RAIN   = 0x40,  /* 矩阵代码雨：黑客帝国代码雨 */
    BLE_LED_EFFECT_PARTICLE      = 0x41,  /* 像素粒子：粒子系统 */
    BLE_LED_EFFECT_LIGHTSABER    = 0x42,  /* 光剑：光剑展开收回 */
    BLE_LED_EFFECT_TETRIS        = 0x43,  /* 俄罗斯方块：方块下落堆积 */
    BLE_LED_EFFECT_CHESS_SCAN    = 0x44,  /* 棋盘扫描：对角线顺序点亮 */
    BLE_LED_EFFECT_RAINBOW_FALL  = 0x45,  /* 彩虹瀑布：彩虹色从顶部流到底部 */
    BLE_LED_EFFECT_CLOCK         = 0x46,  /* 时钟：显示当前时间 HH:MM */
    BLE_LED_EFFECT_MAX
} ble_led_effect_t;

typedef struct __attribute__((packed)) {
    uint8_t effect;
    uint8_t speed;
} ble_led_effect_config_t;

typedef struct __attribute__((packed)) {
    uint8_t on;
    uint8_t effect;
    uint8_t speed;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t rows;
    uint8_t cols;
} ble_led_status_t;

typedef struct __attribute__((packed)) {
    uint8_t rows;
    uint8_t cols;
} ble_led_layout_t;

bool ble_srv_led_init(void);
void ble_srv_led_deinit(void);
bool ble_srv_led_set_on(bool on);
bool ble_srv_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
bool ble_srv_led_set_effect(ble_led_effect_t effect, uint8_t speed);
bool ble_srv_led_set_layout(uint8_t rows, uint8_t cols);
bool ble_srv_led_get_status(ble_led_status_t *status);
bool ble_srv_led_get_layout(ble_led_layout_t *layout);

#ifdef __cplusplus
}
#endif

#endif
