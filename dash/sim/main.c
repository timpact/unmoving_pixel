/*
 * dash PC simulator (ADR 0010).
 *
 *   dash_sim                          window 480×800, mouse = touch
 *   dash_sim --zoom <f>               scale the window (e.g. 0.6), layout stays 480×800
 *   dash_sim --link-drops             simulate a lost core link (2 s every 30 s)
 *   dash_sim --screenshot <dir>       headless: run the simulation and write
 *            [--time <s>]             screenshots (BMP) of all screens
 *            [--measure]              start the distance measurement at t = 5 s
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "sim_core.h"
#include "ui.h"

#define HOR_RES 480
#define VER_RES 800
#define MODEL_PERIOD_MS 50   /* status frames arrive every 50 ms (ADR 0004) */

extern const unsigned char ui_font_data[];
extern const unsigned long ui_font_data_size;

static ui_model_t s_model;

/* ---- headless rendering --------------------------------------------------- */

static uint32_t s_fake_ms;
static uint16_t s_fb[HOR_RES * VER_RES] __attribute__((aligned(64)));

static uint32_t fake_tick(void)
{
    return s_fake_ms;
}

static void headless_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    (void)area;
    (void)px_map;   /* direct mode: LVGL renders straight into s_fb */
    lv_display_flush_ready(disp);
}

static void run_lvgl(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 10) {
        s_fake_ms += 10;
        lv_timer_handler();
    }
}

static int write_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return -1;
    }
    const uint32_t row = HOR_RES * 3;              /* 1440, already 4-byte aligned */
    const uint32_t data = row * VER_RES;
    uint8_t h[54] = {'B', 'M'};
    uint32_t v;
    v = 54 + data; memcpy(&h[2], &v, 4);
    v = 54;        memcpy(&h[10], &v, 4);
    v = 40;        memcpy(&h[14], &v, 4);
    v = HOR_RES;   memcpy(&h[18], &v, 4);
    v = VER_RES;   memcpy(&h[22], &v, 4);
    h[26] = 1;     h[28] = 24;
    v = data;      memcpy(&h[34], &v, 4);
    fwrite(h, 1, sizeof(h), f);

    uint8_t line[HOR_RES * 3];
    for (int y = VER_RES - 1; y >= 0; y--) {       /* BMP is bottom-up */
        for (int x = 0; x < HOR_RES; x++) {
            uint16_t p = s_fb[y * HOR_RES + x];
            uint8_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
            line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
            line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
            line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
        }
        fwrite(line, 1, sizeof(line), f);
    }
    fclose(f);
    printf("wrote %s\n", path);
    return 0;
}

static void snapshot(const char *dir, const char *name)
{
    char path[512];
    lv_obj_invalidate(lv_screen_active());
    run_lvgl(100);
    lv_refr_now(NULL);
    snprintf(path, sizeof(path), "%s/%s.bmp", dir, name);
    write_bmp(path);
}

static void model_tick(void)
{
    sim_core_step(MODEL_PERIOD_MS);
    sim_core_fill_model(&s_model);
    ui_update(&s_model);
}

static int run_headless(const char *dir, uint32_t sim_s, bool measure)
{
    lv_tick_set_cb(fake_tick);
    lv_display_t *disp = lv_display_create(HOR_RES, VER_RES);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, s_fb, NULL, sizeof(s_fb), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, headless_flush);

    ui_init(&sim_core_actions, ui_font_data, ui_font_data_size);

    for (uint32_t t = 0; t < sim_s * 1000u; t += MODEL_PERIOD_MS) {
        if (measure && t == 5000u) {
            sim_core_actions.measure_start(NULL, 0);
        }
        if (measure && t == 12000u) {
            sim_core_actions.measure_start(NULL, 1);
        }
        model_tick();
        run_lvgl(MODEL_PERIOD_MS);
    }

    ui_show_tab(0);
    snapshot(dir, "ride");

    ui_show_tab(1);
    ui_scroll_led_page(false);
    snapshot(dir, "led_top");

    sim_core_set_editor(true);
    model_tick();
    ui_scroll_led_page(true);
    snapshot(dir, "led_bottom");

    ui_show_tab(2);
    snapshot(dir, "settings");
    return 0;
}

/* ---- window mode ---------------------------------------------------------- */

static int run_window(float zoom)
{
    lv_display_t *disp = lv_sdl_window_create(HOR_RES, VER_RES);
    lv_sdl_window_set_title(disp, "unmoving_pixel – dash");
    /* Resizing would change the display resolution and re-layout the GUI.
     * Keep 480×800; use --zoom to scale the window instead. */
    lv_sdl_window_set_resizeable(disp, false);
    if (zoom > 0.0f) {
        lv_sdl_window_set_zoom(disp, zoom);
    }
    lv_sdl_mouse_create();

    ui_init(&sim_core_actions, ui_font_data, ui_font_data_size);

    uint32_t last = lv_tick_get();
    uint32_t acc = 0;
    while (1) {
        uint32_t now = lv_tick_get();
        acc += now - last;
        last = now;
        while (acc >= MODEL_PERIOD_MS) {
            acc -= MODEL_PERIOD_MS;
            model_tick();
        }
        uint32_t wait = lv_timer_handler();
        lv_delay_ms(wait < 5 ? wait : 5);
    }
    return 0;
}

int main(int argc, char **argv)
{
    const char *shot_dir = NULL;
    uint32_t sim_s = 20;
    bool measure = false;
    float zoom = 0.0f;
    sim_options_t opt = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shot_dir = argv[++i];
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            sim_s = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--measure") == 0) {
            measure = true;
        } else if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            zoom = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--link-drops") == 0) {
            opt.link_drops = true;
        } else {
            fprintf(stderr, "usage: %s [--zoom <f>] [--link-drops] [--screenshot <dir> [--time <s>] [--measure]]\n", argv[0]);
            return 2;
        }
    }

    lv_init();
    sim_core_init(&opt);

    return shot_dir ? run_headless(shot_dir, sim_s, measure) : run_window(zoom);
}
