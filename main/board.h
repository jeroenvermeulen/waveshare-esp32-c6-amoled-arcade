#pragma once

#include <stdbool.h>
#include "driver/i2c_master.h"

// Waveshare ESP32-C6-Touch-AMOLED-2.16 pin map (from Waveshare's examples and schematic)
#define PIN_I2C_SCL     7
#define PIN_I2C_SDA     8

#define PIN_LCD_CLK     0
#define PIN_LCD_D0      1
#define PIN_LCD_D1      2
#define PIN_LCD_D2      3
#define PIN_LCD_D3      4
#define PIN_LCD_CS      15

#define PIN_TP_INT      5
#define PIN_TP_RST      11

#define PIN_I2S_MCLK    19
#define PIN_I2S_BCLK    20
#define PIN_I2S_DIN     21
#define PIN_I2S_WS      22
#define PIN_I2S_DOUT    23

// Top buttons. Seen from the front: BOOT/- is left, PWR is middle, KEY/+ is right.
// BOOT and KEY pull low when pressed. PWR goes to the AXP2101 PWRON pin (a long
// press powers off) and, through a transistor, drives GPIO18 high while pressed.
#define PIN_BTN_LEFT    9
#define PIN_BTN_RIGHT   10
#define PIN_BTN_PWR     18

#define LCD_W           480
#define LCD_H           480

void board_init(void);
i2c_master_bus_handle_t board_i2c(void);

// AXP2101 power management
void pmu_lcd_power_cycle(void);
int  pmu_battery_percent(void);  // -1 when no battery is connected
bool pmu_is_charging(void);
bool pmu_usb_powered(void);
void pmu_power_off(void);
