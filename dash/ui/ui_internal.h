/* Internal declarations shared by the GUI source files. */
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "lvgl.h"
#include "ui.h"

/* Colours (dark theme, readable at night) */
#define UI_COL_BG      lv_color_hex(0x0d1117)
#define UI_COL_CARD    lv_color_hex(0x161b22)
#define UI_COL_BORDER  lv_color_hex(0x30363d)
#define UI_COL_TEXT    lv_color_hex(0xe6edf3)
#define UI_COL_MUTED   lv_color_hex(0x8b949e)
#define UI_COL_ACCENT  lv_color_hex(0x2dd4bf)
#define UI_COL_OK      lv_color_hex(0x22c55e)
#define UI_COL_WARN    lv_color_hex(0xf59e0b)
#define UI_COL_DANGER  lv_color_hex(0xef4444)

typedef struct {
    const lv_font_t *small;     /* 18 px: captions, units */
    const lv_font_t *text;      /* 22 px: labels, buttons */
    const lv_font_t *value;     /* 34 px: distances */
    const lv_font_t *measure;   /* 42 px: measurement values */
    const lv_font_t *speed;     /* 120 px: speed, integer part */
    const lv_font_t *speed_dec; /* 72 px: speed, decimal part (60 %) */
} ui_fonts_t;

/* Number with a fixed decimal comma: the integer part is right-aligned to
 * comma_x, the decimals (",dd") follow left-aligned, then an optional unit in a
 * small grey font. All parts share the same baseline. */
typedef struct {
    lv_obj_t *box;
    lv_obj_t *int_part;
    lv_obj_t *rest;
    lv_obj_t *unit;
} ui_fixed_num_t;

extern ui_fonts_t ui_fonts;
extern ui_actions_t ui_actions;

/* Helpers (ui.c) */
lv_obj_t *ui_card(lv_obj_t *parent, const char *title);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);

/* Set a label text only if it changed (avoids needless redraws). */
void ui_set_text(lv_obj_t *label, const char *text);

void ui_fixed_num_create(ui_fixed_num_t *n, lv_obj_t *parent, int32_t width, int32_t comma_x,
                         const lv_font_t *int_font, lv_color_t int_color,
                         const lv_font_t *rest_font, lv_color_t rest_color);
/* text: formatted number such as "1.234,56"; unit (may be NULL) is shown small and grey */
void ui_fixed_num_set(ui_fixed_num_t *n, const char *text, const char *unit);

/* German number formatting: thousands separator '.', decimal comma.
 * value is scaled by 10^decimals, e.g. (12345, 2) → "123,45". */
void ui_fmt_fixed(char *buf, size_t len, int64_t value, int decimals);

/* Local time (seconds since 1970-01-01) <-> calendar, e.g. "25.11.2026 16:30" */
void ui_fmt_datetime(char *buf, size_t len, uint32_t t);
uint32_t ui_make_time(int year, int month, int day, int hour, int minute);
void ui_split_time(uint32_t t, int *year, int *month, int *day, int *hour, int *minute);

/* Pages */
void ui_ride_create(lv_obj_t *tab);
void ui_ride_update(const ui_model_t *m);
void ui_led_create(lv_obj_t *tab);
void ui_led_update(const ui_model_t *m);
void ui_led_scroll(bool to_bottom);
void ui_settings_create(lv_obj_t *tab);
void ui_settings_update(const ui_model_t *m);

#endif /* UI_INTERNAL_H */
