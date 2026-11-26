/* Board support for the Guition JC4880P443, see board.h. */
#include "board.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "esp_cache.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board";

#define PIN_LCD_RESET   GPIO_NUM_5
#define PIN_BACKLIGHT   GPIO_NUM_23
#define PIN_TOUCH_RESET GPIO_NUM_3
#define PIN_I2C_SDA     GPIO_NUM_7
#define PIN_I2C_SCL     GPIO_NUM_8

#define DSI_PHY_LDO_CHAN 3
#define DSI_PHY_LDO_MV   2500
#define DSI_LANES        2
#define DSI_LANE_MBPS    500
#define DPI_CLOCK_MHZ    34

/* ---- display: ST7701 vendor init sequence (ESPHome model JC4880P443) ---------------- */

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[16];
} lcd_cmd_t;

static const lcd_cmd_t k_init[] = {
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x13}},
    {0xEF, 1, {0x08}},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x10}},
    {0xC0, 2, {0x63, 0x00}},
    {0xC1, 2, {0x0D, 0x02}},
    {0xC2, 2, {0x10, 0x08}},
    {0xCC, 1, {0x10}},
    {0xB0, 16, {0x80, 0x09, 0x53, 0x0C, 0xD0, 0x07, 0x0C, 0x09, 0x09, 0x28, 0x06, 0xD4, 0x13, 0x69, 0x2B, 0x71}},
    {0xB1, 16, {0x80, 0x94, 0x5A, 0x10, 0xD3, 0x06, 0x0A, 0x08, 0x08, 0x25, 0x03, 0xD3, 0x12, 0x66, 0x6A, 0x0D}},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x11}},
    {0xB0, 1, {0x5D}},
    {0xB1, 1, {0x58}},
    {0xB2, 1, {0x87}},
    {0xB3, 1, {0x80}},
    {0xB5, 1, {0x4E}},
    {0xB7, 1, {0x85}},
    {0xB8, 1, {0x21}},
    {0xB9, 2, {0x10, 0x1F}},
    {0xBB, 1, {0x03}},
    {0xBC, 1, {0x00}},
    {0xC1, 1, {0x78}},
    {0xC2, 1, {0x78}},
    {0xD0, 1, {0x88}},
    {0xE0, 3, {0x00, 0x3A, 0x02}},
    {0xE1, 11, {0x04, 0xA0, 0x00, 0xA0, 0x05, 0xA0, 0x00, 0xA0, 0x00, 0x40, 0x40}},
    {0xE2, 13, {0x30, 0x00, 0x40, 0x40, 0x32, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00, 0xA0, 0x00}},
    {0xE3, 4, {0x00, 0x00, 0x33, 0x33}},
    {0xE4, 2, {0x44, 0x44}},
    {0xE5, 16, {0x09, 0x2E, 0xA0, 0xA0, 0x0B, 0x30, 0xA0, 0xA0, 0x05, 0x2A, 0xA0, 0xA0, 0x07, 0x2C, 0xA0, 0xA0}},
    {0xE6, 4, {0x00, 0x00, 0x33, 0x33}},
    {0xE7, 2, {0x44, 0x44}},
    {0xE8, 16, {0x08, 0x2D, 0xA0, 0xA0, 0x0A, 0x2F, 0xA0, 0xA0, 0x04, 0x29, 0xA0, 0xA0, 0x06, 0x2B, 0xA0, 0xA0}},
    {0xEB, 7, {0x00, 0x00, 0x4E, 0x4E, 0x00, 0x00, 0x00}},
    {0xEC, 2, {0x08, 0x01}},
    {0xED, 16, {0xB0, 0x2B, 0x98, 0xA4, 0x56, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xF7, 0x65, 0x4A, 0x89, 0xB2, 0x0B}},
    {0xEF, 6, {0x08, 0x08, 0x08, 0x45, 0x3F, 0x54}},
    {0xFF, 5, {0x77, 0x01, 0x00, 0x00, 0x00}},
    {0x3A, 1, {0x55}},   /* COLMOD: RGB565 */
    {0x36, 1, {0x00}},   /* MADCTL: RGB order, no mirroring */
    {0x20, 0, {0}},      /* INVOFF */
};

esp_err_t board_display_init(esp_lcd_panel_handle_t *panel_out, uint16_t **fb0_out, uint16_t **fb1_out)
{
    /* MIPI DSI PHY needs 2.5 V from the internal LDO */
    esp_ldo_channel_handle_t ldo = NULL;
    esp_ldo_channel_config_t ldo_cfg = {.chan_id = DSI_PHY_LDO_CHAN, .voltage_mv = DSI_PHY_LDO_MV};
    esp_err_t err = esp_ldo_acquire_channel(&ldo_cfg, &ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LDO for DSI PHY: %s", esp_err_to_name(err));
        return err;
    }

    /* Hardware reset of the panel */
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_LCD_RESET, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    gpio_set_level(PIN_LCD_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_LCD_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    esp_lcd_dsi_bus_handle_t bus = NULL;
    esp_lcd_dsi_bus_config_t bus_cfg = {
        .bus_id = 0,
        .num_data_lanes = DSI_LANES,
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
        .lane_bit_rate_mbps = DSI_LANE_MBPS,
    };
    err = esp_lcd_new_dsi_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DSI bus: %s", esp_err_to_name(err));
        return err;
    }

    /* Command channel (DBI over DSI) for the controller registers */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_dbi_io_config_t dbi_cfg = {.virtual_channel = 0, .lcd_cmd_bits = 8, .lcd_param_bits = 8};
    err = esp_lcd_new_panel_io_dbi(bus, &dbi_cfg, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DBI io: %s", esp_err_to_name(err));
        return err;
    }
    for (size_t i = 0; i < sizeof(k_init) / sizeof(k_init[0]); i++) {
        err = esp_lcd_panel_io_tx_param(io, k_init[i].cmd, k_init[i].len ? k_init[i].data : NULL, k_init[i].len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "init command 0x%02X: %s", k_init[i].cmd, esp_err_to_name(err));
            return err;
        }
    }
    esp_lcd_panel_io_tx_param(io, 0x11, NULL, 0);   /* SLPOUT */
    vTaskDelay(pdMS_TO_TICKS(120));
    esp_lcd_panel_io_tx_param(io, 0x29, NULL, 0);   /* DISPON */
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Video mode: the DPI engine streams the frame buffer continuously */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_dpi_panel_config_t dpi_cfg = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = DPI_CLOCK_MHZ,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,   /* tear-free: draw into one, show the other (ADR 0019) */
        .video_timing = {
            .h_size = BOARD_LCD_W,
            .v_size = BOARD_LCD_H,
            .hsync_pulse_width = 12,
            .hsync_back_porch = 42,
            .hsync_front_porch = 42,
            .vsync_pulse_width = 2,
            .vsync_back_porch = 8,
            .vsync_front_porch = 166,
        },
    };
    err = esp_lcd_new_panel_dpi(bus, &dpi_cfg, &panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DPI panel: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_lcd_panel_init(panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DPI init: %s", esp_err_to_name(err));
        return err;
    }
    void *fb0 = NULL;
    void *fb1 = NULL;
    esp_lcd_dpi_panel_get_frame_buffer(panel, 2, &fb0, &fb1);
    *panel_out = panel;
    *fb0_out = fb0;
    *fb1_out = fb1;
    ESP_LOGI(TAG, "display %dx%d, frame buffers %p %p", BOARD_LCD_W, BOARD_LCD_H, fb0, fb1);
    return ESP_OK;
}

void board_display_flush(const void *addr, size_t bytes)
{
    esp_cache_msync((void *)addr, bytes, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

/* ---- backlight ----------------------------------------------------------------------- */

#define BL_FREQ_HZ  1000
#define BL_RES      LEDC_TIMER_10_BIT

esp_err_t board_backlight_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_RES,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BL_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&t);
    if (err != ESP_OK) {
        return err;
    }
    ledc_channel_config_t c = {
        .gpio_num = PIN_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    return ledc_channel_config(&c);
}

void board_backlight_set(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t duty = (uint32_t)percent * ((1u << BL_RES) - 1) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/* ---- touch: GT911 -------------------------------------------------------------------- */

#define GT911_REG_STATUS  0x814E
#define GT911_REG_POINT1  0x814F
#define GT911_REG_PRODUCT 0x8140
#define GT911_REG_XMAX    0x8048

static i2c_master_dev_handle_t s_touch;
static uint16_t s_touch_xmax = BOARD_LCD_W;
static uint16_t s_touch_ymax = BOARD_LCD_H;

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    return i2c_master_transmit_receive(s_touch, addr, 2, data, len, 50);
}

static esp_err_t gt911_write8(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    return i2c_master_transmit(s_touch, buf, 3, 50);
}

esp_err_t board_touch_init(void)
{
    /* Reset without driving the interrupt line (its pin is unknown), so the address is probed */
    gpio_config_t rst = {.pin_bit_mask = 1ULL << PIN_TOUCH_RESET, .mode = GPIO_MODE_OUTPUT};
    gpio_config(&rst);
    gpio_set_level(PIN_TOUCH_RESET, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_TOUCH_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(60));

    i2c_master_bus_handle_t bus = NULL;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C bus: %s", esp_err_to_name(err));
        return err;
    }
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(bus, a, 20) == ESP_OK) {
            ESP_LOGI(TAG, "I2C device at 0x%02X", a);
        }
    }
    uint8_t addr = 0;
    if (i2c_master_probe(bus, 0x5D, 50) == ESP_OK) {
        addr = 0x5D;
    } else if (i2c_master_probe(bus, 0x14, 50) == ESP_OK) {
        addr = 0x14;
    } else {
        ESP_LOGE(TAG, "GT911 not found at 0x5D or 0x14");
        return ESP_ERR_NOT_FOUND;
    }
    i2c_device_config_t dev = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr, .scl_speed_hz = 400000};
    err = i2c_master_bus_add_device(bus, &dev, &s_touch);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t id[4] = {0};
    uint8_t res[4] = {0};
    gt911_read(GT911_REG_PRODUCT, id, 4);
    if (gt911_read(GT911_REG_XMAX, res, 4) == ESP_OK) {
        uint16_t xm = (uint16_t)(res[0] | res[1] << 8);
        uint16_t ym = (uint16_t)(res[2] | res[3] << 8);
        if (xm > 0 && ym > 0) {
            s_touch_xmax = xm;
            s_touch_ymax = ym;
        }
    }
    ESP_LOGI(TAG, "touch GT%c%c%c%c at 0x%02X, resolution %ux%u", id[0], id[1], id[2], id[3] ? id[3] : ' ',
             addr, s_touch_xmax, s_touch_ymax);
    return ESP_OK;
}

bool board_touch_read(uint16_t *x, uint16_t *y)
{
    static bool s_touched;
    static uint16_t s_x, s_y;
    if (s_touch == NULL) {
        return false;
    }
    uint8_t status = 0;
    if (gt911_read(GT911_REG_STATUS, &status, 1) != ESP_OK) {
        return false;
    }
    if (status & 0x80) {   /* new data */
        uint8_t n = status & 0x0F;
        if (n > 0) {
            uint8_t p[8];
            if (gt911_read(GT911_REG_POINT1, p, sizeof(p)) == ESP_OK) {
                uint16_t tx = (uint16_t)(p[1] | p[2] << 8);
                uint16_t ty = (uint16_t)(p[3] | p[4] << 8);
                /* scale to the display, in case the controller is configured differently */
                s_x = (uint16_t)((uint32_t)tx * BOARD_LCD_W / s_touch_xmax);
                s_y = (uint16_t)((uint32_t)ty * BOARD_LCD_H / s_touch_ymax);
            }
        }
        s_touched = n > 0;
        gt911_write8(GT911_REG_STATUS, 0);
    }
    if (s_touched) {
        *x = s_x < BOARD_LCD_W ? s_x : BOARD_LCD_W - 1;
        *y = s_y < BOARD_LCD_H ? s_y : BOARD_LCD_H - 1;
    }
    return s_touched;
}
