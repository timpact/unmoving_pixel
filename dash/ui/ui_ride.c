/* Ride screen: speed, trip/total distance, two distance measurements (ADR 0008, 0010). */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define SPEED_MAX_KMH_X10 160   /* arc scale: 0 … 16.0 km/h (4 m/s = 14.4 km/h) */
#define SPEED_BOX_H       280
#define SPEED_COMMA_X     262   /* x of the decimal comma within the 448 px wide box */

typedef struct {
    uint8_t index;
    bool running;
    bool state_valid;
    ui_fixed_num_t value;
    lv_obj_t *state;
    lv_obj_t *btn_startstop;
    lv_obj_t *btn_startstop_icon;
} measure_card_t;

static lv_obj_t *s_link;
static lv_obj_t *s_battery;
static lv_obj_t *s_arc;
static lv_obj_t *s_unit;
static ui_fixed_num_t s_speed;
static ui_fixed_num_t s_trip;
static ui_fixed_num_t s_total;
static measure_card_t s_meas[UI_MEASURE_COUNT];

/* ---- events ----------------------------------------------------------- */

static void startstop_cb(lv_event_t *e)
{
    measure_card_t *c = lv_event_get_user_data(e);
    if (c->running) {
        if (ui_actions.measure_stop) {
            ui_actions.measure_stop(ui_actions.ctx, c->index);
        }
    } else if (ui_actions.measure_start) {
        ui_actions.measure_start(ui_actions.ctx, c->index);
    }
}

static void reset_cb(lv_event_t *e)
{
    measure_card_t *c = lv_event_get_user_data(e);
    if (ui_actions.measure_reset) {
        ui_actions.measure_reset(ui_actions.ctx, c->index);
    }
}

/* ---- layout ----------------------------------------------------------- */

static lv_obj_t *plain_row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 10, 0);
    return r;
}

static void distance_card(lv_obj_t *parent, const char *title, ui_fixed_num_t *num, int decimals)
{
    lv_obj_t *card = ui_card(parent, title);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_width(card, 0);   /* share the row width equally */
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    ui_fixed_num_create(num, card, 194, 104, decimals, ui_fonts.value, UI_COL_TEXT, ui_fonts.value, UI_COL_TEXT);
}

static lv_obj_t *icon_button(lv_obj_t *parent, const char *icon, lv_color_t color, lv_event_cb_t cb,
                             void *user_data, lv_obj_t **icon_out)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 72, 64);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    lv_obj_t *l = ui_label(btn, ui_fonts.text, lv_color_hex(0x0d1117), icon);
    lv_obj_center(l);
    if (icon_out != NULL) {
        *icon_out = l;
    }
    return btn;
}

static void measure_card_create(lv_obj_t *parent, measure_card_t *c, uint8_t index)
{
    char title[32];
    c->index = index;

    lv_obj_t *card = ui_card(parent, NULL);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 2, 0);

    lv_obj_t *head = plain_row(card);
    lv_snprintf(title, sizeof(title), "Messstrecke %d", index + 1);
    ui_label(head, ui_fonts.small, UI_COL_MUTED, title);
    c->state = ui_label(head, ui_fonts.small, UI_COL_MUTED, "");

    lv_obj_t *body = plain_row(card);
    /* room for "-99.999" before the comma and two decimals after it */
    ui_fixed_num_create(&c->value, body, 260, 168, 2, ui_fonts.measure, UI_COL_TEXT, ui_fonts.measure, UI_COL_TEXT);
    lv_obj_t *btns = plain_row(body);
    lv_obj_set_width(btns, 2 * 72 + 10);   /* two 72 px buttons + gap */
    lv_obj_set_flex_align(btns, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    c->btn_startstop = icon_button(btns, LV_SYMBOL_PLAY, UI_COL_OK, startstop_cb, c, &c->btn_startstop_icon);
    icon_button(btns, LV_SYMBOL_REFRESH, UI_COL_MUTED, reset_cb, c, NULL);
}

void ui_ride_create(lv_obj_t *tab)
{
    lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_row(tab, 10, 0);

    /* Status row: link state, battery */
    lv_obj_t *status = plain_row(tab);
    s_link = ui_label(status, ui_fonts.small, UI_COL_MUTED, "");
    s_battery = ui_label(status, ui_fonts.small, UI_COL_MUTED, "");

    /* Speed: arc with large number, fixed comma, smaller grey decimal */
    lv_obj_t *speed_box = lv_obj_create(tab);
    lv_obj_remove_style_all(speed_box);
    lv_obj_set_size(speed_box, lv_pct(100), SPEED_BOX_H);
    lv_obj_remove_flag(speed_box, LV_OBJ_FLAG_SCROLLABLE);

    s_arc = lv_arc_create(speed_box);
    lv_obj_set_size(s_arc, SPEED_BOX_H, SPEED_BOX_H);
    lv_obj_center(s_arc);
    lv_arc_set_bg_angles(s_arc, 135, 45);
    lv_arc_set_range(s_arc, 0, SPEED_MAX_KMH_X10);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, 16, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, 16, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, UI_COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, UI_COL_ACCENT, LV_PART_INDICATOR);

    ui_fixed_num_create(&s_speed, speed_box, 448, SPEED_COMMA_X, 1,
                        ui_fonts.speed, UI_COL_TEXT, ui_fonts.speed_dec, UI_COL_MUTED);
    lv_obj_align(s_speed.box, LV_ALIGN_CENTER, 0, -16);
    s_unit = ui_label(speed_box, ui_fonts.small, UI_COL_MUTED, "km/h");
    lv_obj_align(s_unit, LV_ALIGN_CENTER, 0, 66);
    lv_obj_set_style_text_align(s_unit, LV_TEXT_ALIGN_CENTER, 0);

    /* Trip and total distance, close to the speedometer */
    lv_obj_t *row = plain_row(tab);
    lv_obj_set_style_margin_top(row, -12, 0);
    distance_card(row, "Tagesstrecke", &s_trip, 2);
    distance_card(row, "Gesamt", &s_total, 1);

    /* Two independent distance measurements */
    for (uint8_t i = 0; i < UI_MEASURE_COUNT; i++) {
        measure_card_create(tab, &s_meas[i], i);
    }
}

/* ---- update ----------------------------------------------------------- */

static void fmt_km(char *buf, size_t len, uint32_t meters, int decimals)
{
    int64_t div = 1000;
    for (int i = 0; i < decimals; i++) {
        div /= 10;
    }
    ui_fmt_fixed(buf, len, meters / div, decimals);
}

static void measure_card_update(measure_card_t *c, int32_t mm, bool running, uint8_t decimals)
{
    char buf[32];
    if (decimals > 2) {
        decimals = 2;
    }

    int64_t v = mm;                        /* may be negative in net mode (ADR 0015) */
    int64_t mag = v < 0 ? -v : v;
    if (mag < 100000000) {                 /* < 100 km: metres */
        int64_t div = decimals == 2 ? 10 : decimals == 1 ? 100 : 1000;
        ui_fmt_fixed(buf, sizeof(buf), v / div, decimals);
        ui_fixed_num_set(&c->value, buf, "m");
    } else {                               /* ≥ 100 km: km with metre resolution */
        ui_fmt_fixed(buf, sizeof(buf), v / 1000, 3);
        ui_fixed_num_set(&c->value, buf, "km");
    }

    if (!c->state_valid || running != c->running) {
        c->state_valid = true;
        c->running = running;
        ui_set_text(c->btn_startstop_icon, running ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
        lv_obj_set_style_bg_color(c->btn_startstop, running ? UI_COL_WARN : UI_COL_OK, 0);
        ui_set_text(c->state, running ? "läuft" : "gestoppt");
        lv_obj_set_style_text_color(c->state, running ? UI_COL_OK : UI_COL_MUTED, 0);
    }
}

void ui_ride_update(const ui_model_t *m)
{
    char buf[48];

    ui_set_text(s_link, m->link_ok ? LV_SYMBOL_OK "  core verbunden" : LV_SYMBOL_WARNING "  keine Verbindung");
    lv_obj_set_style_text_color(s_link, m->link_ok ? UI_COL_OK : UI_COL_DANGER, 0);

    if (m->link_ok && m->battery_mv > 0) {   /* battery value comes from core */
        char v[16];
        ui_fmt_fixed(v, sizeof(v), m->battery_mv / 10, 2);
        lv_snprintf(buf, sizeof(buf), LV_SYMBOL_BATTERY_FULL "  %s V", v);
        ui_set_text(s_battery, buf);
    } else {
        ui_set_text(s_battery, "");
    }

    if (m->link_ok) {
        /* mm/s → km/h ×10: v × 3.6 / 100 */
        bool backward = m->speed_mm_s < 0;
        uint32_t mag = (uint32_t)(backward ? -(int32_t)m->speed_mm_s : m->speed_mm_s);
        uint32_t kmh_x10 = (mag * 36u + 500u) / 1000u;
        ui_set_text(s_unit, backward && kmh_x10 > 0 ? "km/h\nrückwärts" : "km/h");
        ui_fmt_fixed(buf, sizeof(buf), kmh_x10, 1);
        ui_fixed_num_set(&s_speed, buf, NULL);
        lv_arc_set_value(s_arc, kmh_x10 > SPEED_MAX_KMH_X10 ? SPEED_MAX_KMH_X10 : (int32_t)kmh_x10);

        fmt_km(buf, sizeof(buf), m->trip_m, 2);
        ui_fixed_num_set(&s_trip, buf, "km");
        fmt_km(buf, sizeof(buf), m->total_m, 1);
        ui_fixed_num_set(&s_total, buf, "km");
    } else {
        ui_fixed_num_set(&s_speed, "--", NULL);
        ui_set_text(s_unit, "km/h");
        lv_arc_set_value(s_arc, 0);
        ui_fixed_num_set(&s_trip, "--", NULL);
        ui_fixed_num_set(&s_total, "--", NULL);
    }

    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        measure_card_update(&s_meas[i], m->measure_mm[i], m->measure_running[i], m->measure_decimals);
    }
}
