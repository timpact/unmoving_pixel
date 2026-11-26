/* LVGL display and input driver, see lvgl_port.h. */
#include "lvgl_port.h"

#include "board.h"
#include "esp_heap_caps.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "lvgl_port";


static esp_lcd_panel_handle_t s_panel;
static int64_t s_frame_start;
static uint32_t s_frames, s_sum_us, s_max_us;

static void render_start_cb(lv_event_t *e)
{
    s_frame_start = esp_timer_get_time();
}

void lvgl_port_stats(uint32_t *frames, uint32_t *avg_us, uint32_t *max_us)
{
    *frames = s_frames;
    *avg_us = s_frames ? s_sum_us / s_frames : 0;
    *max_us = s_max_us;
    s_frames = s_sum_us = s_max_us = 0;
}

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

/* Frame end (ISR): after a buffer switch the old buffer is free for drawing */
static volatile bool s_swap_pending;
static lv_display_t *s_disp;

static bool IRAM_ATTR refresh_done_cb(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *edata, void *ctx)
{
    if (s_swap_pending) {
        s_swap_pending = false;
        lv_display_flush_ready(s_disp);
    }
    return false;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    if (!lv_display_flush_is_last(disp)) {
        lv_display_flush_ready(disp);   /* drawn in place, nothing to copy */
        return;
    }
    uint32_t us = (uint32_t)(esp_timer_get_time() - s_frame_start);
    s_frames++;
    s_sum_us += us;
    if (us > s_max_us) {
        s_max_us = us;
    }
    /* px is the start of the frame buffer LVGL has drawn into: write back the
       cache and switch to it at the next frame end; flush_ready from the ISR then.
       Set pending after the switch request: an ISR in between only delays by a frame. */
    esp_lcd_panel_draw_bitmap(s_panel, 0, 0, BOARD_LCD_W, BOARD_LCD_H, px);
    s_swap_pending = true;
}

/* Benchmark: synthetic finger swipes (vertical, 400 px in 0.4 s, then 0.3 s released) */
static bool s_fake_swipe;
void lvgl_port_fake_swipe(bool on)
{
    s_fake_swipe = on;
}

static void touch_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    uint16_t x, y;
    if (s_fake_swipe) {
        static int dir = 1;
        uint32_t t = (uint32_t)(esp_timer_get_time() / 1000) % 700;
        if (t < 400) {
            data->point.x = 240;
            data->point.y = (int32_t)(dir > 0 ? 650 - t : 250 + t);
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
            if (t > 690) {
                dir = -dir;
            }
        }
        return;
    }
    if (board_touch_read(&x, &y)) {
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

esp_err_t lvgl_port_init(esp_lcd_panel_handle_t panel, void *fb0, void *fb1, bool touch_ok)
{
    s_panel = panel;
    lv_tick_set_cb(tick_ms);

    size_t bytes = BOARD_LCD_W * BOARD_LCD_H * sizeof(uint16_t);
    lv_display_t *disp = lv_display_create(BOARD_LCD_W, BOARD_LCD_H);
    s_disp = disp;
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, fb0, fb1, bytes, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
    esp_lcd_dpi_panel_event_callbacks_t cbs = {.on_refresh_done = refresh_done_cb};
    esp_lcd_dpi_panel_register_event_callbacks(panel, &cbs, NULL);

    if (touch_ok) {
        lv_indev_t *indev = lv_indev_create();
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, touch_cb);
        lv_indev_set_display(indev, disp);
    }
    ESP_LOGI(TAG, "display ready: direct mode, two frame buffers, switch at frame end");
    return ESP_OK;
}
