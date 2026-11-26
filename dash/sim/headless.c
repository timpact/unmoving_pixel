/* Headless rendering for screenshots and checks (see headless.h). */
#include "headless.h"

#include <stdio.h>

#include "lvgl.h"
#include "png.h"

static uint32_t s_fake_ms;
static uint16_t s_fb[HOR_RES * VER_RES] __attribute__((aligned(64)));
static uint8_t s_rgb[HOR_RES * VER_RES * 3];

static uint32_t fake_tick(void)
{
    return s_fake_ms;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;   /* direct mode: LVGL renders straight into s_fb */
    lv_display_flush_ready(disp);
}

void headless_init(void)
{
    lv_tick_set_cb(fake_tick);
    lv_display_t *disp = lv_display_create(HOR_RES, VER_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof(s_fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
}

void headless_run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 10) {
        s_fake_ms += 10;
        lv_timer_handler();
    }
}

void headless_render(void)
{
    lv_obj_invalidate(lv_screen_active());
    lv_obj_invalidate(lv_layer_top());
    headless_run(500);   /* let style transitions and animations finish */
    lv_refr_now(NULL);
}

void headless_get_rgb(uint8_t *rgb)
{
    for (int i = 0; i < HOR_RES * VER_RES; i++) {
        uint16_t p = s_fb[i];
        uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        rgb[i * 3 + 0] = (uint8_t)((r << 3) | (r >> 2));
        rgb[i * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
        rgb[i * 3 + 2] = (uint8_t)((b << 3) | (b >> 2));
    }
}

int headless_write_png(const char *path)
{
    headless_render();
    headless_get_rgb(s_rgb);
    if (png_write_rgb(path, HOR_RES, VER_RES, s_rgb) != 0) {
        fprintf(stderr, "could not write %s\n", path);
        return -1;
    }
    printf("wrote %s\n", path);
    return 0;
}
