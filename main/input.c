#include <stdlib.h>
#include <string.h>
#include "input.h"
#include "board.h"
#include "driver/gpio.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_log.h"
#include "esp_timer.h"

#define SWIPE_DIST      48
#define SWIPE_TIME_US   350000
#define TAP_DIST        28
#define TAP_TIME_US     400000

static const char *TAG = "input";
static esp_lcd_touch_handle_t s_tp;
static bool s_down, s_swiped;
static int s_start_x, s_start_y, s_last_x, s_last_y;
static int64_t s_start_us;
static uint8_t s_prev_btn;

void input_init(void)
{
    gpio_config_t low_active = {
        .pin_bit_mask = (1ULL << PIN_BTN_LEFT) | (1ULL << PIN_BTN_RIGHT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&low_active));
    gpio_config_t high_active = {
        .pin_bit_mask = 1ULL << PIN_BTN_PWR,
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&high_active));

    esp_lcd_panel_io_handle_t tp_io;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    io_cfg.scl_speed_hz = 400000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(board_i2c(), &io_cfg, &tp_io));

    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_W,
        .y_max = LCD_H,
        .rst_gpio_num = PIN_TP_RST,
        .int_gpio_num = PIN_TP_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 1, .mirror_x = 0, .mirror_y = 1 },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, &s_tp));
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

void input_poll(input_t *in)
{
    memset(in, 0, sizeof(*in));

    uint8_t btn = 0;
    if (!gpio_get_level(PIN_BTN_LEFT)) btn |= BTN_LEFT;
    if (!gpio_get_level(PIN_BTN_RIGHT)) btn |= BTN_RIGHT;
    if (gpio_get_level(PIN_BTN_PWR)) btn |= BTN_PWR;
    in->held = btn;
    in->pressed = btn & ~s_prev_btn;
    s_prev_btn = btn;

    esp_lcd_touch_point_data_t pt;
    uint8_t count = 0;
    esp_lcd_touch_read_data(s_tp);
    bool touched = esp_lcd_touch_get_data(s_tp, &pt, &count, 1) == ESP_OK && count > 0;
    int64_t now = esp_timer_get_time();

    if (touched) {
        int tx = clamp(pt.x, 0, LCD_W - 1), ty = clamp(pt.y, 0, LCD_H - 1);
        if (!s_down) {
            s_down = true;
            s_swiped = false;
            s_start_x = tx;
            s_start_y = ty;
            s_start_us = now;
            in->touch_start = true;
        }
        in->touching = true;
        in->x = s_last_x = tx;
        in->y = s_last_y = ty;

        int dx = tx - s_start_x, dy = ty - s_start_y;
        if (!s_swiped && now - s_start_us < SWIPE_TIME_US &&
            (abs(dx) >= SWIPE_DIST || abs(dy) >= SWIPE_DIST)) {
            s_swiped = true;
            if (abs(dx) > abs(dy)) in->swipe = dx > 0 ? SWIPE_RIGHT : SWIPE_LEFT;
            else in->swipe = dy > 0 ? SWIPE_DOWN : SWIPE_UP;
        }
    } else if (s_down) {
        s_down = false;
        in->touch_end = true;
        in->x = s_last_x;
        in->y = s_last_y;
        if (!s_swiped && abs(s_last_x - s_start_x) < TAP_DIST && abs(s_last_y - s_start_y) < TAP_DIST &&
            now - s_start_us < TAP_TIME_US) {
            in->tap = true;
        }
    }

    in->activity = in->touching || in->touch_end || btn || in->pressed;

    if (in->tap) ESP_LOGI(TAG, "tap %d,%d", in->x, in->y);
    if (in->swipe) ESP_LOGI(TAG, "swipe %s", (const char *[]){ "", "left", "right", "up", "down" }[in->swipe]);
    if (in->pressed & BTN_LEFT) ESP_LOGI(TAG, "button left (BOOT, GPIO%d)", PIN_BTN_LEFT);
    if (in->pressed & BTN_RIGHT) ESP_LOGI(TAG, "button right (KEY, GPIO%d)", PIN_BTN_RIGHT);
    if (in->pressed & BTN_PWR) ESP_LOGI(TAG, "button PWR (GPIO%d)", PIN_BTN_PWR);
}
