/*
 * Settings screen (ADR 0010): display brightness, measurement resolution,
 * encoder calibration with history (ADR 0014), date and time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"

#define MIN_REF_CM        500u    /* 5 m minimum reference distance */
#define MIN_COUNTS        1000u
#define WARN_DEVIATION    10.0f   /* % */

/* Settings page */
static lv_obj_t *s_disp_slider;
static lv_obj_t *s_disp_val;
static lv_obj_t *s_resolution;
static lv_obj_t *s_backward;
static lv_obj_t *s_factor;
/* History table: header + UI_CALIB_HISTORY rows, 4 fixed-width columns */
#define HIST_COLS 4
static const int32_t k_hist_w[HIST_COLS] = {150, 78, 88, 100};   /* sum = 416 = card content width */
static lv_obj_t *s_hist_rows[UI_CALIB_HISTORY];
static lv_obj_t *s_hist_cell[UI_CALIB_HISTORY][HIST_COLS];
static lv_obj_t *s_hist_empty;
static lv_obj_t *s_time_val;

/* Calibration dialog */
static lv_obj_t *s_cal;
static lv_obj_t *s_cal_title;
static lv_obj_t *s_cal_live;
static lv_obj_t *s_cal_hint;
static lv_obj_t *s_cal_input_row;
static lv_obj_t *s_cal_ta;
static lv_obj_t *s_cal_result;
static lv_obj_t *s_cal_kb;
static lv_obj_t *s_cal_btn_main;
static lv_obj_t *s_cal_btn_main_label;
static lv_obj_t *s_cal_btn_cancel_label;

/* Time dialog */
static lv_obj_t *s_time_dlg;
static lv_obj_t *s_roller[5];   /* day, month, year, hour, minute */

#define YEAR_MIN 2024
#define YEAR_MAX 2045

/* Mirrored state */
static uint8_t s_cal_state = 0xFF;
static int32_t s_cal_counts;
static uint32_t s_nm_per_count;
static uint32_t s_history_sig = 0xFFFFFFFF;
static bool s_confirm_pending;
static uint32_t s_time_now;
static bool s_time_valid;

/* ---- helpers ---------------------------------------------------------- */

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 12, 0);
    return r;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, lv_color_t color, lv_event_cb_t cb,
                        lv_obj_t **label_out)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_height(btn, 60);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_30, LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *l = ui_label(btn, ui_fonts.text, lv_color_hex(0x0d1117), text);
    lv_obj_center(l);
    if (label_out != NULL) {
        *label_out = l;
    }
    return btn;
}

static lv_obj_t *dialog(void)
{
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_80, 0);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);   /* block touches to the page below */
    lv_obj_add_flag(bg, LV_OBJ_FLAG_HIDDEN);
    return bg;
}

/* Signed percentage with two decimals and German comma, e.g. "-1,21 %" */
static void fmt_percent(char *buf, size_t len, float p)
{
    int64_t v = (int64_t)(p * 100.0f + (p >= 0 ? 0.5f : -0.5f));
    char num[24];
    ui_fmt_fixed(num, sizeof(num), v < 0 ? -v : v, 2);
    lv_snprintf(buf, len, "%s%s %%", v < 0 ? "-" : "+", num);
}

static void fmt_factor(char *buf, size_t len, uint32_t nm)
{
    ui_fmt_fixed(buf, len, nm, 3);   /* nm → µm with 3 decimals */
    strcat(buf, " µm/Schritt");
}

/* ---- calibration ------------------------------------------------------ */

static bool cal_result(uint32_t *nm_out, float *dev_out)
{
    uint32_t ref_cm = (uint32_t)strtoul(lv_textarea_get_text(s_cal_ta), NULL, 10);
    if (ref_cm < MIN_REF_CM || s_cal_counts < (int32_t)MIN_COUNTS) {   /* net counts (ADR 0015) */
        return false;
    }
    uint64_t nm = (uint64_t)ref_cm * 10000000u / (uint32_t)s_cal_counts;   /* 1 cm = 10^7 nm */
    if (nm == 0 || nm > UINT32_MAX) {
        return false;
    }
    *nm_out = (uint32_t)nm;
    *dev_out = s_nm_per_count ? ((float)nm - (float)s_nm_per_count) * 100.0f / (float)s_nm_per_count : 0.0f;
    return true;
}

static void cal_update_result(void)
{
    char buf[128];
    char f[40];
    uint32_t nm;
    float dev;
    s_confirm_pending = false;

    if (s_cal_counts < 0) {
        ui_set_text(s_cal_result, "Netto rückwärts gefahren – bitte neu messen.");
        lv_obj_set_style_text_color(s_cal_result, UI_COL_DANGER, 0);
        lv_obj_add_state(s_cal_btn_main, LV_STATE_DISABLED);
    } else if (s_cal_counts < (int32_t)MIN_COUNTS) {
        ui_set_text(s_cal_result, "Zu wenige Schritte – bitte eine längere Strecke fahren.");
        lv_obj_set_style_text_color(s_cal_result, UI_COL_DANGER, 0);
        lv_obj_add_state(s_cal_btn_main, LV_STATE_DISABLED);
    } else if (!cal_result(&nm, &dev)) {
        ui_set_text(s_cal_result, "Mindestens 500 cm eingeben.");
        lv_obj_set_style_text_color(s_cal_result, UI_COL_MUTED, 0);
        lv_obj_add_state(s_cal_btn_main, LV_STATE_DISABLED);
    } else {
        char pct[24];
        fmt_factor(f, sizeof(f), nm);
        fmt_percent(pct, sizeof(pct), dev);
        bool warn = dev > WARN_DEVIATION || dev < -WARN_DEVIATION;
        lv_snprintf(buf, sizeof(buf), "%sNeuer Faktor: %s\nAbweichung: %s",
                    warn ? LV_SYMBOL_WARNING "  Abweichung über 10 % – Strecke und Eingabe prüfen.\n" : "",
                    f, pct);
        ui_set_text(s_cal_result, buf);
        lv_obj_set_style_text_color(s_cal_result, warn ? UI_COL_WARN : UI_COL_OK, 0);
        lv_obj_remove_state(s_cal_btn_main, LV_STATE_DISABLED);
    }
    ui_set_text(s_cal_btn_main_label, "Übernehmen");
    lv_obj_set_style_bg_color(s_cal_btn_main, UI_COL_OK, 0);
}

static void cal_start_cb(lv_event_t *e)
{
    (void)e;
    if (ui_actions.calib_start) {
        ui_actions.calib_start(ui_actions.ctx);
    }
}

static void cal_main_cb(lv_event_t *e)
{
    (void)e;
    if (s_cal_state == UI_CALIB_COUNTING) {
        if (ui_actions.calib_stop) {
            ui_actions.calib_stop(ui_actions.ctx);
        }
        return;
    }
    uint32_t nm;
    float dev;
    if (!cal_result(&nm, &dev)) {
        return;
    }
    bool warn = dev > WARN_DEVIATION || dev < -WARN_DEVIATION;
    if (warn && !s_confirm_pending) {
        s_confirm_pending = true;   /* large deviation: ask a second time */
        char buf[200];
        lv_snprintf(buf, sizeof(buf), "%s\nTrotzdem übernehmen? Zum Bestätigen erneut tippen.",
                    lv_label_get_text(s_cal_result));
        ui_set_text(s_cal_result, buf);
        lv_obj_set_style_bg_color(s_cal_btn_main, UI_COL_WARN, 0);
        ui_set_text(s_cal_btn_main_label, "Bestätigen");
        return;
    }
    if (ui_actions.calib_apply) {
        ui_actions.calib_apply(ui_actions.ctx, (uint32_t)strtoul(lv_textarea_get_text(s_cal_ta), NULL, 10));
    }
}

static void cal_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (ui_actions.calib_discard) {
        ui_actions.calib_discard(ui_actions.ctx);
    }
}

static void cal_ta_cb(lv_event_t *e)
{
    (void)e;
    cal_update_result();
}

static void cal_create(void)
{
    s_cal = dialog();

    lv_obj_t *card = ui_card(s_cal, NULL);
    lv_obj_set_width(card, 448);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 16);

    s_cal_title = ui_label(card, ui_fonts.text, UI_COL_TEXT, "");
    s_cal_live = ui_label(card, ui_fonts.value, UI_COL_TEXT, "");
    s_cal_hint = ui_label(card, ui_fonts.small, UI_COL_MUTED, "");
    lv_label_set_long_mode(s_cal_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_cal_hint, lv_pct(100));

    s_cal_input_row = row(card);
    ui_label(s_cal_input_row, ui_fonts.text, UI_COL_TEXT, "Tatsächlich:");
    s_cal_ta = lv_textarea_create(s_cal_input_row);
    lv_textarea_set_one_line(s_cal_ta, true);
    lv_textarea_set_accepted_chars(s_cal_ta, "0123456789");
    lv_textarea_set_max_length(s_cal_ta, 6);
    lv_textarea_set_placeholder_text(s_cal_ta, "1000");
    lv_obj_set_flex_grow(s_cal_ta, 1);
    lv_obj_set_style_text_font(s_cal_ta, ui_fonts.value, 0);
    lv_obj_add_event_cb(s_cal_ta, cal_ta_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_label(s_cal_input_row, ui_fonts.text, UI_COL_MUTED, "cm");

    s_cal_result = ui_label(card, ui_fonts.small, UI_COL_MUTED, "");
    lv_label_set_long_mode(s_cal_result, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_cal_result, lv_pct(100));

    lv_obj_t *btns = row(card);
    lv_obj_t *b = button(btns, "", UI_COL_OK, cal_main_cb, &s_cal_btn_main_label);
    lv_obj_set_flex_grow(b, 1);
    s_cal_btn_main = b;
    b = button(btns, "", UI_COL_MUTED, cal_cancel_cb, &s_cal_btn_cancel_label);
    lv_obj_set_flex_grow(b, 1);

    /* Digits only: 1–9, backspace, 0, OK */
    static const char *kb_map[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n",
                                   LV_SYMBOL_BACKSPACE, "0", LV_SYMBOL_OK, ""};
    static const lv_buttonmatrix_ctrl_t kb_ctrl[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    s_cal_kb = lv_keyboard_create(s_cal);
    lv_keyboard_set_map(s_cal_kb, LV_KEYBOARD_MODE_USER_1, kb_map, kb_ctrl);
    lv_keyboard_set_mode(s_cal_kb, LV_KEYBOARD_MODE_USER_1);
    lv_keyboard_set_textarea(s_cal_kb, s_cal_ta);
    lv_obj_set_size(s_cal_kb, lv_pct(100), 300);
    lv_obj_align(s_cal_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_cal_kb, ui_fonts.value, 0);
}

static void cal_show_state(uint8_t state)
{
    switch (state) {
    case UI_CALIB_COUNTING:
        lv_obj_remove_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(s_cal_title, LV_SYMBOL_GPS "  Kalibrierfahrt läuft");
        ui_set_text(s_cal_hint, "Strecke in m nach bisherigem Faktor. Eine bekannte Strecke fahren "
                                "oder schieben (z. B. 10 m entlang eines Maßbands), dann Stopp drücken. "
                                "Überfahren und Zurückrollen ist erlaubt, es zählt die Netto-Strecke.");
        lv_obj_add_flag(s_cal_input_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cal_result, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_cal_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(s_cal_btn_main, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_cal_btn_main, UI_COL_WARN, 0);
        ui_set_text(s_cal_btn_main_label, LV_SYMBOL_STOP "  Stopp");
        ui_set_text(s_cal_btn_cancel_label, "Abbrechen");
        break;
    case UI_CALIB_STOPPED:
        lv_obj_remove_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
        ui_set_text(s_cal_title, LV_SYMBOL_EDIT "  Strecke eingeben");
        ui_set_text(s_cal_hint, "Tatsächlich gefahrene Strecke in cm eingeben.");
        lv_obj_remove_flag(s_cal_input_row, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_cal_result, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_cal_kb, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(s_cal_ta, "");
        lv_obj_add_state(s_cal_ta, LV_STATE_FOCUSED);
        lv_obj_set_style_bg_color(s_cal_btn_main, UI_COL_OK, 0);
        ui_set_text(s_cal_btn_cancel_label, "Verwerfen");
        cal_update_result();
        break;
    default:
        lv_obj_add_flag(s_cal, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

/* ---- time dialog -------------------------------------------------------- */

static void roller_options(lv_obj_t *r, int from, int to, bool two_digits)
{
    static char buf[256];
    buf[0] = '\0';
    for (int v = from; v <= to; v++) {
        char item[8];
        lv_snprintf(item, sizeof(item), two_digits ? "%02d%s" : "%d%s", v, v < to ? "\n" : "");
        strcat(buf, item);
    }
    lv_roller_set_options(r, buf, LV_ROLLER_MODE_NORMAL);
}

static void time_open_cb(lv_event_t *e)
{
    (void)e;
    int y = 2026, mo = 1, d = 1, h = 12, mi = 0;
    if (s_time_valid) {
        ui_split_time(s_time_now, &y, &mo, &d, &h, &mi);
    }
    if (y < YEAR_MIN) y = YEAR_MIN;
    if (y > YEAR_MAX) y = YEAR_MAX;
    lv_roller_set_selected(s_roller[0], (uint32_t)(d - 1), LV_ANIM_OFF);
    lv_roller_set_selected(s_roller[1], (uint32_t)(mo - 1), LV_ANIM_OFF);
    lv_roller_set_selected(s_roller[2], (uint32_t)(y - YEAR_MIN), LV_ANIM_OFF);
    lv_roller_set_selected(s_roller[3], (uint32_t)h, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller[4], (uint32_t)mi, LV_ANIM_OFF);
    lv_obj_remove_flag(s_time_dlg, LV_OBJ_FLAG_HIDDEN);
}

static void time_ok_cb(lv_event_t *e)
{
    (void)e;
    int d = (int)lv_roller_get_selected(s_roller[0]) + 1;
    int mo = (int)lv_roller_get_selected(s_roller[1]) + 1;
    int y = (int)lv_roller_get_selected(s_roller[2]) + YEAR_MIN;
    int h = (int)lv_roller_get_selected(s_roller[3]);
    int mi = (int)lv_roller_get_selected(s_roller[4]);
    static const int mdays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maxd = mdays[mo - 1] + (mo == 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)));
    if (d > maxd) {
        d = maxd;
    }
    if (ui_actions.set_time) {
        ui_actions.set_time(ui_actions.ctx, ui_make_time(y, mo, d, h, mi));
    }
    lv_obj_add_flag(s_time_dlg, LV_OBJ_FLAG_HIDDEN);
}

static void time_cancel_cb(lv_event_t *e)
{
    (void)e;
    lv_obj_add_flag(s_time_dlg, LV_OBJ_FLAG_HIDDEN);
}

static void time_dialog_create(void)
{
    s_time_dlg = dialog();
    lv_obj_t *card = ui_card(s_time_dlg, "Datum und Uhrzeit");
    lv_obj_set_width(card, 448);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t *r = row(card);
    lv_obj_set_style_pad_column(r, 6, 0);
    static const char *captions[] = {"Tag", "Monat", "Jahr", "Std", "Min"};
    for (int i = 0; i < 5; i++) {
        lv_obj_t *col = lv_obj_create(r);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        ui_label(col, ui_fonts.small, UI_COL_MUTED, captions[i]);
        s_roller[i] = lv_roller_create(col);
        lv_obj_set_style_text_font(s_roller[i], ui_fonts.text, 0);
        lv_roller_set_visible_row_count(s_roller[i], 3);
        lv_obj_set_width(s_roller[i], i == 2 ? 100 : 70);
    }
    roller_options(s_roller[0], 1, 31, true);
    roller_options(s_roller[1], 1, 12, true);
    roller_options(s_roller[2], YEAR_MIN, YEAR_MAX, false);
    roller_options(s_roller[3], 0, 23, true);
    roller_options(s_roller[4], 0, 59, true);

    lv_obj_t *btns = row(card);
    lv_obj_set_flex_grow(button(btns, "OK", UI_COL_OK, time_ok_cb, NULL), 1);
    lv_obj_set_flex_grow(button(btns, "Abbrechen", UI_COL_MUTED, time_cancel_cb, NULL), 1);
}

/* ---- page --------------------------------------------------------------- */

static void history_table_create(lv_obj_t *card);

static void disp_cb(lv_event_t *e)
{
    int32_t v = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%d %%", (int)v);
    ui_set_text(s_disp_val, buf);
    if (ui_actions.set_display_brightness) {
        ui_actions.set_display_brightness(ui_actions.ctx, (uint8_t)v);
    }
}

static void backward_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (ui_actions.set_measure_add_backward) {
        ui_actions.set_measure_add_backward(ui_actions.ctx, sel == 1);
    }
}

static void resolution_cb(lv_event_t *e)
{
    uint32_t sel = lv_dropdown_get_selected(lv_event_get_target(e));
    if (ui_actions.set_measure_decimals) {
        ui_actions.set_measure_decimals(ui_actions.ctx, (uint8_t)sel);
    }
}

void ui_settings_create(lv_obj_t *tab)
{
    /* Display brightness */
    lv_obj_t *card = ui_card(tab, NULL);
    lv_obj_t *r = row(card);
    ui_label(r, ui_fonts.small, UI_COL_MUTED, "Display-Helligkeit");
    s_disp_val = ui_label(r, ui_fonts.text, UI_COL_TEXT, "");
    s_disp_slider = lv_slider_create(card);
    lv_slider_set_range(s_disp_slider, 5, 100);   /* never fully dark */
    lv_obj_set_size(s_disp_slider, lv_pct(100), 14);
    lv_obj_add_event_cb(s_disp_slider, disp_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_style_pad_bottom(card, 24, 0);

    /* Measurement resolution (index = number of decimals in metres) */
    card = ui_card(tab, "Messstrecken");
    ui_label(card, ui_fonts.small, UI_COL_TEXT, "Auflösung");
    s_resolution = lv_dropdown_create(card);
    lv_obj_set_width(s_resolution, lv_pct(100));
    lv_dropdown_set_options(s_resolution, "1 m\n10 cm\n1 cm");
    lv_obj_set_style_bg_color(s_resolution, UI_COL_BG, 0);
    lv_obj_set_style_border_color(s_resolution, UI_COL_BORDER, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_resolution), ui_fonts.text, 0);
    lv_obj_add_event_cb(s_resolution, resolution_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Backward travel in measurements (ADR 0015) */
    ui_label(card, ui_fonts.small, UI_COL_TEXT, "Rückwärtsfahrt");
    s_backward = lv_dropdown_create(card);
    lv_obj_set_width(s_backward, lv_pct(100));
    lv_dropdown_set_options(s_backward, "abziehen (Netto-Strecke)\naddieren (gefahrene Strecke)");
    lv_obj_set_style_bg_color(s_backward, UI_COL_BG, 0);
    lv_obj_set_style_border_color(s_backward, UI_COL_BORDER, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_backward), ui_fonts.text, 0);
    lv_obj_add_event_cb(s_backward, backward_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Encoder calibration (ADR 0014) */
    card = ui_card(tab, "Drehgeber-Kalibrierung");
    s_factor = ui_label(card, ui_fonts.text, UI_COL_TEXT, "");
    lv_obj_t *b = button(card, LV_SYMBOL_GPS "  Kalibrierfahrt starten", UI_COL_ACCENT, cal_start_cb, NULL);
    lv_obj_set_width(b, lv_pct(100));
    ui_label(card, ui_fonts.small, UI_COL_MUTED, "Verlauf, Faktor in µm/Schritt (neueste zuerst)");
    history_table_create(card);

    /* Date and time (no RTC on the board, ADR 0014) */
    card = ui_card(tab, "Datum und Uhrzeit");
    r = row(card);
    s_time_val = ui_label(r, ui_fonts.text, UI_COL_TEXT, "");
    b = button(r, "Stellen", UI_COL_MUTED, time_open_cb, NULL);
    lv_obj_set_width(b, 140);

    cal_create();
    time_dialog_create();
}

static lv_obj_t *hist_row(lv_obj_t *parent, lv_obj_t **cells, lv_color_t color)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_ver(r, 5, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, UI_COL_BORDER, 0);
    for (int c = 0; c < HIST_COLS; c++) {
        cells[c] = ui_label(r, ui_fonts.small, color, "");
        lv_obj_set_width(cells[c], k_hist_w[c]);
        lv_obj_set_style_text_align(cells[c], c == 0 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT, 0);
    }
    return r;
}

static void history_table_create(lv_obj_t *card)
{
    lv_obj_t *table = lv_obj_create(card);
    lv_obj_remove_style_all(table);
    lv_obj_set_size(table, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(table, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *head[HIST_COLS];
    hist_row(table, head, UI_COL_MUTED);
    lv_label_set_text(head[0], "Datum");
    lv_label_set_text(head[1], "km");
    lv_label_set_text(head[2], "Faktor");
    lv_label_set_text(head[3], "Änderung");

    for (int i = 0; i < UI_CALIB_HISTORY; i++) {
        s_hist_rows[i] = hist_row(table, s_hist_cell[i], UI_COL_TEXT);
        lv_obj_add_flag(s_hist_rows[i], LV_OBJ_FLAG_HIDDEN);
    }
    s_hist_empty = ui_label(table, ui_fonts.small, UI_COL_MUTED, "noch keine Kalibrierung");
}

void ui_calib_test_input(const char *ref_cm, bool press_main)
{
    if (ref_cm != NULL) {
        lv_textarea_set_text(s_cal_ta, ref_cm);   /* sends LV_EVENT_VALUE_CHANGED */
    }
    if (press_main) {
        lv_obj_send_event(s_cal_btn_main, LV_EVENT_CLICKED, NULL);
    }
}

static void history_update(const ui_model_t *m)
{
    /* cheap signature to detect changes */
    uint32_t sig = m->calib_history_count * 2654435761u ^ m->nm_per_count;
    for (int i = 0; i < m->calib_history_count; i++) {
        sig = sig * 31u + m->calib_history[i].nm_per_count + m->calib_history[i].timestamp;
    }
    if (sig == s_history_sig) {
        return;
    }
    s_history_sig = sig;

    char f[40];
    char buf[160];
    fmt_factor(f, sizeof(f), m->nm_per_count);
    lv_snprintf(buf, sizeof(buf), "Faktor: %s", f);
    ui_set_text(s_factor, buf);

    int n = m->calib_history_count < UI_CALIB_HISTORY ? m->calib_history_count : UI_CALIB_HISTORY;
    if (n == 0) {
        lv_obj_remove_flag(s_hist_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_hist_empty, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < UI_CALIB_HISTORY; i++) {
        if (i >= n) {
            lv_obj_add_flag(s_hist_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_remove_flag(s_hist_rows[i], LV_OBJ_FLAG_HIDDEN);
        const ui_calib_entry_t *c = &m->calib_history[i];
        char cell[32];

        if (c->timestamp != 0) {
            int y, mo, d, h, mi;
            ui_split_time(c->timestamp, &y, &mo, &d, &h, &mi);
            lv_snprintf(cell, sizeof(cell), "%02d.%02d.%02d %02d:%02d", d, mo, y % 100, h, mi);
        } else {
            strcpy(cell, "unbekannt");
        }
        ui_set_text(s_hist_cell[i][0], cell);

        ui_fmt_fixed(cell, sizeof(cell), c->total_m / 1000u, 0);
        ui_set_text(s_hist_cell[i][1], cell);

        ui_fmt_fixed(cell, sizeof(cell), c->nm_per_count, 3);
        ui_set_text(s_hist_cell[i][2], cell);

        cell[0] = '\0';
        if (i + 1 < n && m->calib_history[i + 1].nm_per_count != 0) {
            float d = ((float)c->nm_per_count - (float)m->calib_history[i + 1].nm_per_count) * 100.0f /
                      (float)m->calib_history[i + 1].nm_per_count;
            fmt_percent(cell, sizeof(cell), d);
        } else {
            strcpy(cell, "–");
        }
        ui_set_text(s_hist_cell[i][3], cell);
    }
}

void ui_settings_update(const ui_model_t *m)
{
    char buf[48];

    if (!lv_obj_has_state(s_disp_slider, LV_STATE_PRESSED)) {
        if (lv_slider_get_value(s_disp_slider) != m->display_brightness) {
            lv_slider_set_value(s_disp_slider, m->display_brightness, LV_ANIM_OFF);
        }
        lv_snprintf(buf, sizeof(buf), "%d %%", m->display_brightness);
        ui_set_text(s_disp_val, buf);
    }
    if (!lv_dropdown_is_open(s_resolution) && lv_dropdown_get_selected(s_resolution) != m->measure_decimals) {
        lv_dropdown_set_selected(s_resolution, m->measure_decimals);
    }
    uint32_t bw = m->measure_add_backward ? 1 : 0;
    if (!lv_dropdown_is_open(s_backward) && lv_dropdown_get_selected(s_backward) != bw) {
        lv_dropdown_set_selected(s_backward, bw);
    }

    /* Calibration */
    s_nm_per_count = m->nm_per_count;
    history_update(m);
    if (m->calib_state != s_cal_state) {
        s_cal_state = m->calib_state;
        cal_show_state(s_cal_state);
    }
    if (s_cal_state != UI_CALIB_IDLE) {
        char cnt[24];
        char dist[24];
        ui_fmt_fixed(cnt, sizeof(cnt), m->calib_counts, 0);
        ui_fmt_fixed(dist, sizeof(dist), m->calib_distance_mm / 10, 2);
        lv_snprintf(buf, sizeof(buf), "%s Schritte\n≈ %s m", cnt, dist);
        ui_set_text(s_cal_live, buf);
        if (m->calib_counts != s_cal_counts) {
            s_cal_counts = m->calib_counts;
            if (s_cal_state == UI_CALIB_STOPPED) {
                cal_update_result();
            }
        }
    }

    /* Clock */
    s_time_valid = m->time_valid;
    s_time_now = m->time_now;
    if (m->time_valid) {
        ui_fmt_datetime(buf, sizeof(buf), m->time_now);
    } else {
        strcpy(buf, "nicht gestellt");
    }
    ui_set_text(s_time_val, buf);
}
