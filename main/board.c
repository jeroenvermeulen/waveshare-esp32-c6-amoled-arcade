#include "board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AXP2101_ADDR        0x34

#define AXP_STATUS1         0x00    // bit5 VBUS good, bit3 battery present
#define AXP_STATUS2         0x01    // bits7:5 = 1 charging, 2 discharging
#define AXP_PWR_CTRL        0x10    // bit0 = power off
#define AXP_ADC_CTRL        0x30    // bit0 = battery voltage ADC
#define AXP_INTEN1          0x40
#define AXP_INTEN2          0x41
#define AXP_INTEN3          0x42
#define AXP_INTSTS1         0x48
#define AXP_INTSTS2         0x49
#define AXP_INTSTS3         0x4A
#define AXP_BAT_DET         0x68    // bit0 = battery detection
#define AXP_LDO_EN          0x90    // bit0..3 = ALDO1..4
#define AXP_ALDO1_V         0x92    // 0x92..0x95 = ALDO1..4, (mV - 500) / 100
#define AXP_BAT_PERCENT     0xA4

#define ALDO3_LCD           0x04

static const char *TAG = "board";

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_pmu;

static void pmu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (i2c_master_transmit(s_pmu, buf, sizeof(buf), 100) != ESP_OK) {
        ESP_LOGW(TAG, "PMU write 0x%02x failed", reg);
    }
}

static int pmu_read(uint8_t reg)
{
    uint8_t val;
    if (i2c_master_transmit_receive(s_pmu, &reg, 1, &val, 1, 100) != ESP_OK) {
        return -1;
    }
    return val;
}

static void pmu_set_bits(uint8_t reg, uint8_t bits)
{
    int v = pmu_read(reg);
    if (v >= 0) {
        pmu_write(reg, v | bits);
    }
}

void board_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &s_bus));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AXP2101_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev_cfg, &s_pmu));

    // ALDO1..4 feed the mic, display and touch: 3.3 V, all on
    for (uint8_t reg = AXP_ALDO1_V; reg < AXP_ALDO1_V + 4; reg++) {
        pmu_write(reg, (3300 - 500) / 100);
    }
    pmu_set_bits(AXP_LDO_EN, 0x0F);

    pmu_set_bits(AXP_ADC_CTRL, 0x01);
    pmu_set_bits(AXP_BAT_DET, 0x01);

    // No PMU interrupts needed: the PWR button is read through GPIO18
    pmu_write(AXP_INTEN1, 0x00);
    pmu_write(AXP_INTEN2, 0x00);
    pmu_write(AXP_INTEN3, 0x00);
    pmu_write(AXP_INTSTS1, 0xFF);
    pmu_write(AXP_INTSTS2, 0xFF);
    pmu_write(AXP_INTSTS3, 0xFF);

    ESP_LOGI(TAG, "PMU ready, battery %d%%", pmu_battery_percent());
}

i2c_master_bus_handle_t board_i2c(void)
{
    return s_bus;
}

void pmu_lcd_power_cycle(void)
{
    int en = pmu_read(AXP_LDO_EN);
    if (en < 0) {
        return;
    }
    pmu_write(AXP_LDO_EN, en & ~ALDO3_LCD);
    vTaskDelay(pdMS_TO_TICKS(100));
    pmu_write(AXP_LDO_EN, en | ALDO3_LCD);
    vTaskDelay(pdMS_TO_TICKS(100));
}

int pmu_battery_percent(void)
{
    int s1 = pmu_read(AXP_STATUS1);
    if (s1 < 0 || !(s1 & 0x08)) {
        return -1;
    }
    return pmu_read(AXP_BAT_PERCENT);
}

bool pmu_is_charging(void)
{
    int s2 = pmu_read(AXP_STATUS2);
    return s2 >= 0 && ((s2 >> 5) & 0x07) == 1;
}

bool pmu_usb_powered(void)
{
    int s1 = pmu_read(AXP_STATUS1);
    return s1 >= 0 && (s1 & 0x20);
}

void pmu_power_off(void)
{
    pmu_set_bits(AXP_PWR_CTRL, 0x01);
}
