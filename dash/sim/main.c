/*
 * dash PC simulator (ADR 0010).
 *
 *   dash_sim                          window 480×800, mouse = touch
 *   dash_sim --zoom <f>               scale the window (e.g. 0.6), layout stays 480×800
 *   dash_sim --link-drops             simulate a lost core link (2 s every 30 s)
 *   dash_sim --overlays-full          all user overlay slots taken: changing a built-in
 *                                     overlay shows the "no free slot" notice
 *   dash_sim --screenshot <dir>       headless: run the simulation and write
 *            [--time <s>]             screenshots (PNG) of all screens
 *            [--measure]              start the distance measurements at t = 5 s / 12 s
 *   dash_sim --check                  headless GUI checks against docs/ui-guidelines.md
 *            [--update-references]    and the reference screenshots (ADR 0016)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "headless.h"
#include "lvgl.h"
#include "sim_core.h"
#include "ui.h"

#define MODEL_PERIOD_MS 50   /* status frames arrive every 50 ms (ADR 0004) */

extern const unsigned char ui_font_data[];
extern const unsigned long ui_font_data_size;

static ui_model_t s_model;

static void snapshot(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.png", dir, name);
    headless_write_png(path);
}

static void model_tick(void)
{
    sim_core_step(MODEL_PERIOD_MS);
    sim_core_fill_model(&s_model);
    ui_update(&s_model);
}

static int run_headless(const char *dir, uint32_t sim_ms, bool measure)
{
    headless_init();

    ui_init(&sim_core_actions, ui_font_data, ui_font_data_size);

    for (uint32_t t = 0; t < sim_ms; t += MODEL_PERIOD_MS) {
        if (measure && t == 5000u) {
            sim_core_actions.measure_start(NULL, 0);
        }
        if (measure && t == 12000u) {
            sim_core_actions.measure_start(NULL, 1);
        }
        model_tick();
        headless_run(MODEL_PERIOD_MS);
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

    /* Calibration: test ride for 4 s, then stop */
    sim_core_actions.calib_start(NULL);
    for (int i = 0; i < 80; i++) {
        model_tick();
        headless_run(MODEL_PERIOD_MS);
    }
    snapshot(dir, "calib_counting");
    sim_core_actions.calib_stop(NULL);
    model_tick();
    snapshot(dir, "calib_input");

    /* Wrong reference (10 m instead of ~15 m): warning, then confirmation step */
    ui_calib_test_input("1000", false);
    snapshot(dir, "calib_warn");
    ui_calib_test_input(NULL, true);
    snapshot(dir, "calib_confirm");
    ui_calib_test_input("1520", false);

    /* Apply a reference of 15 m (the simulated ride was ~15.2 m) and set the clock */
    sim_core_actions.set_time(NULL, 1795000000u);
    sim_core_actions.calib_apply(NULL, 1520);
    model_tick();
    snapshot(dir, "settings_after_calib");
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
    bool check = false;
    bool update_refs = false;
    uint32_t sim_ms = 20000;
    bool measure = false;
    float zoom = 0.0f;
    sim_options_t opt = {0};

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shot_dir = argv[++i];
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            sim_ms = (uint32_t)(atof(argv[++i]) * 1000.0);
        } else if (strcmp(argv[i], "--measure") == 0) {
            measure = true;
        } else if (strcmp(argv[i], "--zoom") == 0 && i + 1 < argc) {
            zoom = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--check") == 0) {
            check = true;
        } else if (strcmp(argv[i], "--update-references") == 0) {
            update_refs = true;
        } else if (strcmp(argv[i], "--link-drops") == 0) {
            opt.link_drops = true;
        } else if (strcmp(argv[i], "--overlays-full") == 0) {
            opt.overlays_full = true;
        } else {
            fprintf(stderr, "usage: %s [--zoom <f>] [--link-drops] [--overlays-full] [--screenshot <dir> [--time <s>] [--measure]]\n"
                            "       %s --check [--update-references]\n", argv[0], argv[0]);
            return 2;
        }
    }

    lv_init();
    if (check) {
        return check_run(update_refs);
    }
    sim_core_init(&opt);

    return shot_dir ? run_headless(shot_dir, sim_ms, measure) : run_window(zoom);
}
