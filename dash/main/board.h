/*
 * Board support for the Guition JC4880P443 (ESP32-P4), only ESP-IDF drivers.
 *
 * Pins and display parameters (from the ESPHome model JC4880P443 and two working
 * ESPHome device configurations; to be verified on the board):
 *   display   ST7701, MIPI DSI 2 lanes, 500 Mbit/s per lane, 480 × 800, RGB565
 *   DSI PHY   internal LDO channel 3, 2.5 V
 *   reset     GPIO5 (display), GPIO3 (touch)
 *   backlight GPIO23 (LEDC PWM)
 *   touch     GT911 on I2C, SDA GPIO7, SCL GPIO8, 400 kHz
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

#define BOARD_LCD_W 480
#define BOARD_LCD_H 800

/* Display: power, reset, ST7701 init, DPI video mode with two frame buffers (RGB565,
 * PSRAM). draw_bitmap() with one of them switches to it at the next frame end. */
esp_err_t board_display_init(esp_lcd_panel_handle_t *panel, uint16_t **fb0, uint16_t **fb1);

/* Write back the CPU cache of a frame buffer region so the DPI DMA sees it */
void board_display_flush(const void *addr, size_t bytes);

esp_err_t board_backlight_init(void);
void board_backlight_set(uint8_t percent);   /* 0 … 100 */

esp_err_t board_touch_init(void);
/* true while touched; x/y in display coordinates (0 … 479, 0 … 799) */
bool board_touch_read(uint16_t *x, uint16_t *y);

#endif /* BOARD_H */
