/*
 * dash firmware (ADR 0019): display, touch and the GUI from dash/ui.
 *
 * Model: dash_state (dash settings in NVS, measurements, ride panel) plus the
 * core part from:
 *   DASH_DEMO_CORE (menuconfig, default on until the UART link exists): the
 *   simulated core of the PC simulator plays back the test ride, so the GUI can
 *   be looked at on the device without core.
 *
 * The GUI task runs the LVGL timer handler and updates the model every 50 ms
 * (STATUS rate of core, ADR 0004).
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "dash_state.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "lvgl_port.h"
#include "sdkconfig.h"
#include "settings.h"
#include "ui.h"

#if CONFIG_DASH_DEMO_CORE
#include "sim_core.h"
#endif

static const char *TAG = "dash";

#define MODEL_PERIOD_MS 50
#define GUI_STACK       (16 * 1024)

extern const uint8_t font_ttf_start[] asm("_binary_Montserrat_Medium_ttf_start");
extern const uint8_t font_ttf_end[] asm("_binary_Montserrat_Medium_ttf_end");

static ui_model_t s_model;
static ui_actions_t s_actions;   /* core actions + dash-owned actions (dash_state) */

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* one model update: core part, then the dash-owned part */
static void model_update(uint32_t dt_ms)
{
    uint32_t total = 0, position = 0;
#if CONFIG_DASH_DEMO_CORE
    sim_core_step(dt_ms);
    sim_core_counters(&total, &position);
    sim_core_fill_model(&s_model);
#else
    (void)dt_ms;   /* UART link (ADR 0019, step 5) */
#endif
    dash_state_update(now_ms(), total, position);
    dash_state_fill_model(&s_model);
    ui_update(&s_model);
}

typedef struct {
    esp_lcd_panel_handle_t panel;
    uint16_t *fb[2];
} gui_args_t;

static void gui_task(void *arg)
{
    gui_args_t *args = arg;
    bool touch_ok = board_touch_init() == ESP_OK;

    lv_init();
    if (lvgl_port_init(args->panel, args->fb[0], args->fb[1], touch_ok) != ESP_OK) {
        vTaskDelete(NULL);
        return;
    }

    dash_state_init(settings_store());
#if CONFIG_DASH_DEMO_CORE
    sim_options_t opt = {0};
    sim_core_init(&opt);
    dash_state_actions(&s_actions, &sim_core_actions);
    ESP_LOGI(TAG, "GUI running with the demo core (simulated test ride)");
#else
    static const ui_actions_t no_core = {0};
    dash_state_actions(&s_actions, &no_core);
#endif
    ui_init(&s_actions, font_ttf_start, (size_t)(font_ttf_end - font_ttf_start));

#if CONFIG_DASH_BENCH_SCROLL
#if CONFIG_DASH_DEMO_CORE && CONFIG_DASH_BENCH_MEASURE
    /* reproduce: start a distance measurement on the ride page, run, stop, reset */
    ui_show_tab(UI_TAB_RIDE);
    for (int i = 0; i < 100; i++) {   /* 2 s */
        if (i == 0) s_actions.set_ride_panel(NULL, UI_RIDE_PANEL_MEASURE);
        if (i == 10) s_actions.measure_start(NULL, 0);
        if (i == 80) s_actions.measure_stop(NULL, 0);
        if (i == 90) s_actions.measure_reset(NULL, 0);
        model_update(20);
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
#endif
    ui_show_tab(CONFIG_DASH_BENCH_TAB);
    int64_t bench_end = esp_timer_get_time() + 12 * 1000000;
    int64_t bench_next = esp_timer_get_time() + 2 * 1000000;
    int bench_dir = 1;
#endif
    uint8_t backlight = 0xFF;
    TickType_t next_model = xTaskGetTickCount();
    uint32_t frames = 0;
    int64_t t_stat = esp_timer_get_time();
    uint32_t max_handler_us = 0;
    while (true) {
        if ((int32_t)(xTaskGetTickCount() - next_model) >= 0) {
            next_model += pdMS_TO_TICKS(MODEL_PERIOD_MS);
            model_update(MODEL_PERIOD_MS);
            if (s_model.display_brightness != backlight) {   /* dash setting (ADR 0010) */
                backlight = s_model.display_brightness;
                board_backlight_set(backlight);
            }
        }
#if CONFIG_DASH_BENCH_SCROLL
        /* continuous scrolling: every frame is a full redraw */
        if (bench_end != 0 && esp_timer_get_time() >= bench_next) {
            static int step;
            if (step == 0) {
                uint32_t f, avg, mx;
                lvgl_port_stats(&f, &avg, &mx);   /* start of the measurement */
            }
            lvgl_port_fake_swipe(true);   /* finger swipes instead of programmatic scrolling */
            if (++step % 60 == 0) {
                bench_dir = -bench_dir;
            }
            if (esp_timer_get_time() >= bench_end) {
                uint32_t f, avg, mx;
                lvgl_port_stats(&f, &avg, &mx);
                ESP_LOGI(TAG, "bench: %" PRIu32 " frames in 10 s (%.1f fps), frame avg %" PRIu32 " ms, max %" PRIu32 " ms",
                         f, f / 10.0, avg / 1000, mx / 1000);
                bench_end = 0;
                lvgl_port_fake_swipe(false);
            }
        }
#endif
        int64_t t0 = esp_timer_get_time();
        uint32_t wait = lv_timer_handler();
        uint32_t dt = (uint32_t)(esp_timer_get_time() - t0);
        if (dt > max_handler_us) {
            max_handler_us = dt;
        }
        frames++;
        if (esp_timer_get_time() - t_stat >= 10 * 1000000) {
            ESP_LOGI(TAG, "lv_timer_handler: %" PRIu32 " calls/10 s, max %" PRIu32 " ms, free internal %u KB, PSRAM %u KB",
                     frames, max_handler_us / 1000,
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
            frames = 0;
            max_handler_us = 0;
            t_stat = esp_timer_get_time();
        }
        if (wait > 10) {
            wait = 10;
        }
        vTaskDelay(pdMS_TO_TICKS(wait > 0 ? wait : 1));
    }
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "dash: chip v%d.%d, flash %" PRIu32 " MB, PSRAM %u MB", chip.revision / 100, chip.revision % 100,
             flash_size / (1024 * 1024), (unsigned)(esp_psram_get_size() / (1024 * 1024)));

    ESP_ERROR_CHECK(board_backlight_init());
    board_backlight_set(0);   /* dark until the first frame is drawn */

    static gui_args_t args;
    ESP_ERROR_CHECK(board_display_init(&args.panel, &args.fb[0], &args.fb[1]));
    for (int i = 0; i < 2; i++) {
        memset(args.fb[i], 0, BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t));
        board_display_flush(args.fb[i], BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t));
    }

    xTaskCreatePinnedToCore(gui_task, "gui", GUI_STACK, &args, 5, NULL, 1);
}
