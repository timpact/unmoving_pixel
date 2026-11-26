/* Ride screen: speed, trip/total distance, two distance measurements (ADR 0008, 0010). */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define SPEED_MAX_KMH     16    /* gauge range: 0 … 16 km/h (4 m/s = 14.4 km/h) */
#define SPEED_MAX_KMH_X10 (SPEED_MAX_KMH * 10)
#define SPEED_BOX_H       258   /* scale numbers may reach beyond the box (overflow visible) */
#define GAUGE_D           244   /* arc diameter */
#define GAUGE_W           14    /* arc width */
#define GAUGE_START       135   /* degrees, 0 km/h (LVGL: 0° = right, clockwise) */
#define GAUGE_SWEEP       270
#define TICK_MAJOR        10    /* tick length at 0, 5, 10, 15 km/h */
#define TICK_MINOR        5     /* tick length at every km/h */
#define LABEL_R           (GAUGE_D / 2 + TICK_MAJOR + 16)   /* radius of the scale numbers */
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

/* Switchable area "Messung | Licht" and lights (ADR 0018) */
static lv_obj_t *s_seg_btn[2];
static lv_obj_t *s_seg_label[2];
static lv_obj_t *s_panel[2];
static lv_obj_t *s_sig_btn[3];         /* left, hazard, right */
static lv_obj_t *s_sig_label[3][2];    /* icon, caption */
static lv_obj_t *s_light_btn;
static lv_obj_t *s_light_label;
static lv_obj_t *s_brake_label;
static lv_obj_t *s_arrow[2];           /* turn signal indicator left/right */
static uint8_t s_signal;
static bool s_light_on;
static uint8_t s_panel_shown = 0xFF;

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

static void panel_cb(lv_event_t *e)
{
    uint8_t panel = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (ui_actions.set_ride_panel) {
        ui_actions.set_ride_panel(ui_actions.ctx, panel);
    }
}

/* Tapping the active signal again switches it off (ADR 0018) */
static void signal_cb(lv_event_t *e)
{
    uint8_t sig = (uint8_t)(intptr_t)lv_event_get_user_data(e);
    if (ui_actions.set_signal) {
        ui_actions.set_signal(ui_actions.ctx, s_signal == sig ? UP_SIGNAL_OFF : sig);
    }
}

static void light_cb(lv_event_t *e)
{
    if (ui_actions.set_light) {
        ui_actions.set_light(ui_actions.ctx, !s_light_on);
    }
}

/* ---- layout ----------------------------------------------------------- */

static lv_obj_t *plain_row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    /* a layout row must not scroll itself: it would swallow vertical swipes
       (slider knobs overhang the row) instead of passing them to the page (I4) */
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
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

static lv_obj_t *flat_button(lv_obj_t *parent, int32_t h, lv_event_cb_t cb, void *user_data)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, h);
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_width(btn, 0);   /* share the row equally */
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, UI_COL_BORDER, 0);
    lv_obj_set_style_pad_all(btn, 4, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
    return btn;
}

static void set_button_active(lv_obj_t *btn, lv_obj_t *const *labels, int n, bool active, lv_color_t on_color)
{
    ui_set_bg_color(btn, active ? on_color : UI_COL_BORDER);
    for (int i = 0; i < n; i++) {
        ui_set_text_color(labels[i], active ? UI_COL_BG : UI_COL_TEXT);
    }
}

static void lights_panel_create(lv_obj_t *parent)
{
    static const char *const icons[3] = {LV_SYMBOL_LEFT, LV_SYMBOL_WARNING, LV_SYMBOL_RIGHT};
    static const char *const captions[3] = {"Blinker links", "Warnblinker", "Blinker rechts"};
    static const uint8_t signals[3] = {UP_SIGNAL_LEFT, UP_SIGNAL_HAZARD, UP_SIGNAL_RIGHT};

    lv_obj_t *card = ui_card(parent, NULL);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 10, 0);

    lv_obj_t *r = plain_row(card);
    for (int i = 0; i < 3; i++) {
        s_sig_btn[i] = flat_button(r, 80, signal_cb, (void *)(intptr_t)signals[i]);
        lv_obj_set_flex_flow(s_sig_btn[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(s_sig_btn[i], LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(s_sig_btn[i], 2, 0);
        s_sig_label[i][0] = ui_label(s_sig_btn[i], ui_fonts.text, UI_COL_TEXT, icons[i]);
        s_sig_label[i][1] = ui_label(s_sig_btn[i], ui_fonts.small, UI_COL_TEXT, captions[i]);
    }

    r = plain_row(card);
    s_light_btn = flat_button(r, 64, light_cb, NULL);
    s_light_label = ui_label(s_light_btn, ui_fonts.text, UI_COL_TEXT, "");
    lv_obj_center(s_light_label);
    s_brake_label = ui_label(r, ui_fonts.small, UI_COL_DANGER, LV_SYMBOL_STOP "  Bremslicht");
    lv_obj_set_width(s_brake_label, 150);
    lv_obj_set_style_text_align(s_brake_label, LV_TEXT_ALIGN_CENTER, 0);
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
    lv_obj_add_flag(speed_box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    s_arc = lv_arc_create(speed_box);
    lv_obj_set_size(s_arc, GAUGE_D, GAUGE_D);
    lv_obj_center(s_arc);
    lv_arc_set_bg_angles(s_arc, GAUGE_START, (GAUGE_START + GAUGE_SWEEP) % 360);
    lv_arc_set_range(s_arc, 0, SPEED_MAX_KMH_X10);
    lv_arc_set_value(s_arc, 0);
    lv_obj_remove_style(s_arc, NULL, LV_PART_KNOB);
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(s_arc, GAUGE_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, GAUGE_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, UI_COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, UI_COL_ACCENT, LV_PART_INDICATOR);

    /* Scale outside the ring: a tick at every km/h, longer ticks and numbers at 0, 5, 10, 15 */
    lv_obj_t *scale = lv_scale_create(speed_box);
    lv_obj_set_size(scale, GAUGE_D, GAUGE_D);
    lv_obj_center(scale);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_OUTER);
    lv_scale_set_range(scale, 0, SPEED_MAX_KMH);
    lv_scale_set_total_tick_count(scale, SPEED_MAX_KMH + 1);
    lv_scale_set_major_tick_every(scale, 5);
    lv_scale_set_label_show(scale, false);   /* numbers are separate labels (checked by ADR 0016) */
    lv_scale_set_angle_range(scale, GAUGE_SWEEP);
    lv_scale_set_rotation(scale, GAUGE_START);
    lv_obj_remove_flag(scale, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);
    lv_obj_set_style_length(scale, TICK_MINOR, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale, 2, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, UI_COL_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, TICK_MAJOR, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, UI_COL_MUTED, LV_PART_INDICATOR);

    for (int v = 0; v <= SPEED_MAX_KMH; v += 5) {
        char txt[4];
        lv_snprintf(txt, sizeof(txt), "%d", v);
        float a = (GAUGE_START + (float)v / SPEED_MAX_KMH * GAUGE_SWEEP) * 3.14159265f / 180.0f;
        lv_obj_t *l = ui_label(speed_box, ui_fonts.small, UI_COL_MUTED, txt);
        lv_obj_align(l, LV_ALIGN_CENTER, (int32_t)lroundf(LABEL_R * cosf(a)), (int32_t)lroundf(LABEL_R * sinf(a)));
    }

    ui_fixed_num_create(&s_speed, speed_box, 448, SPEED_COMMA_X, 1,
                        ui_fonts.speed, UI_COL_TEXT, ui_fonts.speed_dec, UI_COL_MUTED);
    lv_obj_align(s_speed.box, LV_ALIGN_CENTER, 0, -16);
    /* Top-aligned, so that "km/h" stays put when "rückwärts" is added below (N7) */
    s_unit = ui_label(speed_box, ui_fonts.small, UI_COL_MUTED, "km/h");
    lv_obj_align(s_unit, LV_ALIGN_TOP_MID, 0, SPEED_BOX_H / 2 + 60);
    lv_obj_set_style_text_align(s_unit, LV_TEXT_ALIGN_CENTER, 0);

    /* Turn signal indicator (ADR 0018), outside the scale on both sides */
    s_arrow[0] = ui_label(speed_box, ui_fonts.value, UI_COL_WARN, LV_SYMBOL_LEFT);
    lv_obj_align(s_arrow[0], LV_ALIGN_LEFT_MID, 8, 0);
    s_arrow[1] = ui_label(speed_box, ui_fonts.value, UI_COL_WARN, LV_SYMBOL_RIGHT);
    lv_obj_align(s_arrow[1], LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_add_flag(s_arrow[0], LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_arrow[1], LV_OBJ_FLAG_HIDDEN);

    /* Trip and total distance, close to the speedometer */
    lv_obj_t *row = plain_row(tab);
    lv_obj_set_style_margin_top(row, -12, 0);
    distance_card(row, "Tagesstrecke", &s_trip, 2);
    distance_card(row, "Gesamt", &s_total, 1);

    /* Switchable area: distance measurements or lights (ADR 0018) */
    lv_obj_t *seg = plain_row(tab);
    static const char *const seg_text[2] = {"Messung", "Licht"};
    for (int i = 0; i < 2; i++) {
        s_seg_btn[i] = flat_button(seg, 64, panel_cb, (void *)(intptr_t)i);
        s_seg_label[i] = ui_label(s_seg_btn[i], ui_fonts.text, UI_COL_TEXT, seg_text[i]);
        lv_obj_center(s_seg_label[i]);
    }

    for (int i = 0; i < 2; i++) {
        s_panel[i] = lv_obj_create(tab);
        lv_obj_remove_style_all(s_panel[i]);
        lv_obj_set_size(s_panel[i], lv_pct(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(s_panel[i], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(s_panel[i], 10, 0);
    }
    /* Two independent distance measurements */
    for (uint8_t i = 0; i < UI_MEASURE_COUNT; i++) {
        measure_card_create(s_panel[UI_RIDE_PANEL_MEASURE], &s_meas[i], i);
    }
    lights_panel_create(s_panel[UI_RIDE_PANEL_LIGHTS]);
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
    } else {                               /* ≥ 100 km: km with 10 m resolution (2 decimals, N3) */
        ui_fmt_fixed(buf, sizeof(buf), v / 10000, 2);
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
    ui_set_text_color(s_link, m->link_ok ? UI_COL_OK : UI_COL_DANGER);

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

    /* Switchable area */
    uint8_t panel = m->ride_panel == UI_RIDE_PANEL_LIGHTS ? UI_RIDE_PANEL_LIGHTS : UI_RIDE_PANEL_MEASURE;
    if (panel != s_panel_shown) {
        s_panel_shown = panel;
        for (int i = 0; i < 2; i++) {
            ui_set_hidden(s_panel[i], i != panel);
            set_button_active(s_seg_btn[i], &s_seg_label[i], 1, i == panel, UI_COL_ACCENT);
        }
    }

    /* Lights (ADR 0018) */
    s_signal = m->link_ok ? (m->lights & UP_LIGHTS_SIGNAL_MASK) : UP_SIGNAL_OFF;
    s_light_on = m->link_ok && (m->lights & UP_LIGHTS_LIGHT_ON);
    static const uint8_t signals[3] = {UP_SIGNAL_LEFT, UP_SIGNAL_HAZARD, UP_SIGNAL_RIGHT};
    for (int i = 0; i < 3; i++) {
        set_button_active(s_sig_btn[i], s_sig_label[i], 2, s_signal == signals[i], UI_COL_WARN);
    }
    set_button_active(s_light_btn, &s_light_label, 1, s_light_on, UI_COL_ACCENT);
    ui_set_text(s_light_label, s_light_on ? "Licht an" : "Licht aus");
    bool brake = m->link_ok && (m->lights & UP_LIGHTS_BRAKE_ON);
    lv_opa_t brake_opa = brake ? LV_OPA_COVER : LV_OPA_TRANSP;   /* keeps its place in the row */
    if (lv_obj_get_style_opa(s_brake_label, LV_PART_MAIN) != brake_opa) {
        lv_obj_set_style_opa(s_brake_label, brake_opa, 0);
    }

    bool blink = m->link_ok && (m->lights & UP_LIGHTS_BLINK_ON);
    for (int side = 0; side < 2; side++) {
        bool active = s_signal == UP_SIGNAL_HAZARD ||
                      s_signal == (side == 0 ? UP_SIGNAL_LEFT : UP_SIGNAL_RIGHT);
        ui_set_hidden(s_arrow[side], !(active && blink));
    }
}
