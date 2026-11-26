/*
 * LVGL display and input driver on top of board.c (ADR 0019).
 *
 * Rendering (ADR 0019): LVGL draws directly into the DPI frame buffer that is not
 * shown (direct mode, two frame buffers in PSRAM). After the last area of a frame
 * the display switches to it at the next frame end (no tearing); LVGL waits for
 * that before it draws again. LVGL copies the changed areas into the other buffer.
 * Touch: GT911 polled by the LVGL input device.
 */
#ifndef LVGL_PORT_H
#define LVGL_PORT_H

#include <stdbool.h>

#include "esp_err.h"
#include "esp_lcd_types.h"

/* Call after lv_init() and board_display_init()/board_touch_init() */
esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel, void *fb0, void *fb1, bool touch_ok);

/* Frame statistics since the last call: frames, average and maximum frame time in µs */
void lvgl_port_stats(uint32_t *frames, uint32_t *avg_us, uint32_t *max_us);

/* Benchmark: replace the touch input by synthetic vertical swipes */
void lvgl_port_fake_swipe(bool on);

#endif /* LVGL_PORT_H */
