#ifndef BLE_SRV_LED_H
#define BLE_SRV_LED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_LED_SVC_UUID              0xFFB0
#define BLE_LED_CTRL_CHAR_UUID        0xFFB1
#define BLE_LED_COLOR_CHAR_UUID       0xFFB2
#define BLE_LED_EFFECT_CHAR_UUID      0xFFB3

typedef enum {
    BLE_LED_CTRL_OFF = 0x00,
    BLE_LED_CTRL_ON  = 0x01,
} ble_led_ctrl_t;

typedef enum {
    BLE_LED_EFFECT_NONE     = 0x00,
    BLE_LED_EFFECT_BREATH   = 0x01,
    BLE_LED_EFFECT_BLINK    = 0x02,
    BLE_LED_EFFECT_RAINBOW  = 0x03,
    BLE_LED_EFFECT_STROBE   = 0x04,
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
} ble_led_status_t;

bool ble_srv_led_init(void);
void ble_srv_led_deinit(void);
bool ble_srv_led_set_on(bool on);
bool ble_srv_led_set_rgb(uint8_t red, uint8_t green, uint8_t blue);
bool ble_srv_led_set_effect(ble_led_effect_t effect, uint8_t speed);
bool ble_srv_led_get_status(ble_led_status_t *status);

#ifdef __cplusplus
}
#endif

#endif
