/*
 * dash GUI (ADR 0010).
 *
 * Platform-independent: uses only LVGL. The app (ESP-IDF firmware or the PC
 * simulator) fills a ui_model_t and calls ui_update() periodically; user actions
 * are reported through the ui_actions_t callbacks.
 */
#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "up_lights.h"
#include "up_overlay.h"
#include "up_strip.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_MAX_PRESETS     48
#define UI_PRESET_NAME_LEN 17   /* 16 bytes + terminator */

#define UI_PRESET_PATTERN 0
#define UI_PRESET_OVERLAY 1

/* Notices on the LED screen, set and cleared by the app (ADR 0012, 0017) */
#define UI_LED_NOTICE_NONE         0
#define UI_LED_NOTICE_NO_FREE_SLOT 1   /* built-in overlay changed, but no user slot free */

/* Switchable area on the ride screen (ADR 0018) */
#define UI_RIDE_PANEL_MEASURE 0
#define UI_RIDE_PANEL_LIGHTS  1

#define UI_MEASURE_COUNT 2        /* independent distance measurements (ADR 0008) */

/* Encoder calibration (ADR 0014) */
#define UI_CALIB_IDLE     0
#define UI_CALIB_COUNTING 1
#define UI_CALIB_STOPPED  2
#define UI_CALIB_HISTORY  10

typedef struct {
    uint32_t timestamp;      /* local time, seconds since 1970-01-01; 0 = unknown */
    uint32_t total_m;        /* total distance at calibration time */
    uint32_t ref_cm;         /* entered reference distance */
    uint32_t counts;         /* counted encoder steps */
    uint32_t nm_per_count;   /* resulting factor */
} ui_calib_entry_t;

typedef struct {
    uint8_t slot;
    bool    builtin;
    char    name[UI_PRESET_NAME_LEN];
} ui_preset_entry_t;

typedef struct {
    uint16_t          rev;     /* changes whenever the list changes */
    uint8_t           count;
    ui_preset_entry_t entries[UI_MAX_PRESETS];
} ui_preset_list_t;

typedef struct {
    /* Link to core (ADR 0004): false → show "--" */
    bool link_ok;

    /* Ride values */
    int16_t  speed_mm_s;        /* smoothed speed for display; negative = backward (ADR 0015) */
    uint32_t trip_m;
    uint32_t total_m;
    uint16_t battery_mv;        /* 0 = unknown */

    /* Distance measurements (ADR 0008), already computed by the app */
    int32_t  measure_mm[UI_MEASURE_COUNT];   /* may be negative in net mode */
    bool     measure_running[UI_MEASURE_COUNT];

    /* Encoder calibration (ADR 0014), reported by core */
    uint8_t  calib_state;             /* UI_CALIB_* */
    int32_t  calib_counts;            /* net counts of the running test ride (ADR 0015) */
    int32_t  calib_distance_mm;       /* same distance with the current factor */
    uint32_t nm_per_count;            /* current factor */
    uint8_t  calib_history_count;     /* newest entry first */
    ui_calib_entry_t calib_history[UI_CALIB_HISTORY];

    /* Lights (ADR 0018), reported by core */
    uint8_t  lights;                  /* STATUS byte: UP_LIGHTS_* bits (up_lights.h) */
    uint8_t  light_brightness;        /* 10 … 100 % */
    bool     brake_enabled;
    uint8_t  brake_threshold;         /* 0.1 m/s², 5 … 30 */

    /* Strip layout (ADR 0018), stored on core */
    uint8_t  main_leds;               /* UP_STRIP_MAIN_MIN … MAX */
    uint8_t  front_leds;              /* 0 … UP_STRIP_END_MAX */
    uint8_t  rear_leds;
    uint8_t  layout_flags;            /* UP_LAYOUT_* */

    /* Clock on the dash (no RTC: valid only after it was set) */
    bool     time_valid;
    uint32_t time_now;                /* local time, seconds since 1970-01-01 */

    /* dash settings (stored on the dash) */
    uint8_t display_brightness;   /* 5 … 100 % */
    uint8_t measure_decimals;     /* resolution of the measurement display in metres:
                                     0 = 1 m, 1 = 10 cm, 2 = 1 cm */
    bool    measure_add_backward; /* false: backward travel is subtracted (default),
                                     true: backward travel is added (ADR 0015) */
    uint8_t ride_panel;           /* UI_RIDE_PANEL_* (ADR 0018) */

    /* LED settings (ADR 0012) */
    uint8_t      global_brightness;   /* 0 … 100 % */
    up_overlay_t overlay;             /* active overlay */
    uint8_t      active_pattern;      /* slot */
    uint8_t      active_overlay;      /* slot; slider changes are written into this
                                         preset, a built-in one is copied first (ADR 0017) */
    uint8_t      led_notice;          /* UI_LED_NOTICE_* */
    uint16_t     strip_length_mm;     /* main part length, for the overlay preview (ADR 0018) */

    ui_preset_list_t patterns;
    ui_preset_list_t overlays;

    /* Preset editor via Wi-Fi (ADR 0013) */
    bool editor_on;
    char wifi_ssid[33];
    char wifi_password[65];
    char editor_url[64];
    uint8_t  wifi_clients;        /* connected devices */
    uint16_t editor_remaining_s;  /* > 0: Wi-Fi switches off in this many seconds (last 2 min) */
} ui_model_t;

typedef struct {
    void *ctx;
    void (*measure_start)(void *ctx, uint8_t index);
    void (*measure_stop)(void *ctx, uint8_t index);
    void (*measure_reset)(void *ctx, uint8_t index);
    void (*set_display_brightness)(void *ctx, uint8_t percent);
    void (*set_measure_decimals)(void *ctx, uint8_t decimals);
    void (*set_measure_add_backward)(void *ctx, bool add);
    void (*set_brightness)(void *ctx, uint8_t percent);
    /* writes the values into the active overlay preset; for a built-in preset the
       app creates a user copy "<name> (2)" first (ADR 0017) */
    void (*set_overlay)(void *ctx, const up_overlay_t *overlay);
    void (*select_preset)(void *ctx, uint8_t category, uint8_t slot);
    void (*set_editor)(void *ctx, bool on);
    void (*new_wifi_password)(void *ctx);                             /* ADR 0013 */
    void (*calib_start)(void *ctx);
    void (*calib_stop)(void *ctx);
    void (*calib_apply)(void *ctx, uint32_t ref_cm);   /* the app adds the timestamp */
    void (*calib_discard)(void *ctx);
    void (*set_time)(void *ctx, uint32_t local_time);
    /* ADR 0018 */
    void (*set_ride_panel)(void *ctx, uint8_t panel);                  /* stored on the dash */
    void (*set_signal)(void *ctx, uint8_t signal);                     /* UP_SIGNAL_* */
    void (*set_light)(void *ctx, bool on);
    void (*set_strip_layout)(void *ctx, uint8_t main_leds, uint8_t front_leds, uint8_t rear_leds,
                             uint8_t layout_flags);
    void (*set_light_settings)(void *ctx, uint8_t brightness, bool brake_enabled, uint8_t threshold);
} ui_actions_t;

/*
 * Create the GUI on the default display.
 * font_data/font_size: TrueType font (Montserrat) used for all text.
 */
void ui_init(const ui_actions_t *actions, const void *font_data, size_t font_size);

/* Refresh the widgets from the model. Call from the LVGL thread, e.g. every 50 ms. */
void ui_update(const ui_model_t *model);

/* For the simulator, screenshots and GUI checks (ADR 0016) */
void ui_show_tab(uint32_t index);
void ui_scroll_led_page(bool to_bottom);
void ui_scroll_page(bool to_bottom);            /* active tab */
void ui_scroll_page_y(int32_t y);               /* active tab, scroll position in px */
struct _lv_obj_t *ui_get_tab(uint32_t index);
void ui_time_dialog_test(bool open);

/* Object flag marking labels that may wrap (hints, explanations; rule T2) */
#define UI_FLAG_WRAP_ALLOWED LV_OBJ_FLAG_USER_1
/* Simulate keypad input and a tap on the main button of the calibration dialog */
void ui_calib_test_input(const char *ref_cm, bool press_main);

#define UI_TAB_RIDE     0
#define UI_TAB_LED      1
#define UI_TAB_SETTINGS 2

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
