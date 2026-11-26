/* Headless rendering for screenshots and checks (ADR 0010, 0016). */
#ifndef SIM_HEADLESS_H
#define SIM_HEADLESS_H

#include <stdint.h>

#define HOR_RES 480
#define VER_RES 800

/* Create an off-screen 480×800 RGB565 display with a simulated clock. */
void headless_init(void);

/* Advance the simulated clock and run LVGL for ms milliseconds. */
void headless_run(uint32_t ms);

/* Redraw everything, let animations/transitions settle, render now. */
void headless_render(void);

/* Current frame as 8-bit RGB (HOR_RES × VER_RES × 3 bytes). */
void headless_get_rgb(uint8_t *rgb);

/* Render and write the current frame as PNG. Returns 0 on success. */
int headless_write_png(const char *path);

#endif /* SIM_HEADLESS_H */
