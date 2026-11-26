/* LED screen: brightness, pattern list, overlay, preset editor (ADR 0010–0013). */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

#define CHART_POINTS 72   /* one point per LED (144 LEDs/m, 0.5 m) */

typedef enum {
    OV_LEVEL_START,
    OV_LEVEL_MID,
    OV_LEVEL_END,
    OV_FADE_START,
    OV_FADE_END,
    OV_COUNT
} ov_slider_t;

static lv_obj_t *s_page;
static lv_obj_t *s_brightness;
static lv_obj_t *s_brightness_val;
static lv_obj_t *s_pattern_dd;
static lv_obj_t *s_overlay_dd;
static lv_obj_t *s_overlay_hint;
static lv_obj_t *s_chart;
static lv_chart_series_t *s_series;
static lv_obj_t *s_ov_slider[OV_COUNT];
static lv_obj_t *s_ov_val[OV_COUNT];
static lv_obj_t *s_editor_sw;
static lv_obj_t *s_editor_panel;
static lv_obj_t *s_qr;
static lv_obj_t *s_wifi_text;

/* State mirrored from the model */
static uint16_t s_pattern_rev = 0xFFFF;
static uint16_t s_overlay_rev = 0xFFFF;
static uint8_t s_pattern_slots[UI_MAX_PRESETS];
static uint8_t s_overlay_slots[UI_MAX_PRESETS];
static uint8_t s_pattern_count;
static uint8_t s_overlay_count;
static bool s_overlay_builtin[UI_MAX_PRESETS];
static up_overlay_t s_overlay;
static bool s_overlay_valid;
static uint16_t s_strip_mm = 500;
static char s_qr_data[128];

/* ---- helpers ---------------------------------------------------------- */

static bool user_is_touching(lv_obj_t *obj)
{
    return lv_obj_has_state(obj, LV_STATE_PRESSED);
}

static void set_slider(lv_obj_t *slider, int32_t value)
{
    if (!user_is_touching(slider) && lv_slider_get_value(slider) != value) {
        lv_slider_set_value(slider, value, LV_ANIM_OFF);
    }
}

static void update_chart(void)
{
    for (int i = 0; i < CHART_POINTS; i++) {
        float s = (i + 0.5f) * (float)s_strip_mm / CHART_POINTS;
        float f = up_overlay_factor(&s_overlay, s, (float)s_strip_mm);
        lv_chart_set_series_value_by_id(s_chart, s_series, i, (int32_t)(f * 100.0f + 0.5f));
    }
    lv_chart_refresh(s_chart);
}

static void update_overlay_labels(void)
{
    char buf[24];
    lv_snprintf(buf, sizeof(buf), "%d %%", s_overlay.level_start);
    ui_set_text(s_ov_val[OV_LEVEL_START], buf);
    lv_snprintf(buf, sizeof(buf), "%d %%", s_overlay.level_mid);
    ui_set_text(s_ov_val[OV_LEVEL_MID], buf);
    lv_snprintf(buf, sizeof(buf), "%d %%", s_overlay.level_end);
    ui_set_text(s_ov_val[OV_LEVEL_END], buf);
    ui_fmt_fixed(buf, sizeof(buf), s_overlay.fade_start_mm, 0);
    strcat(buf, " mm");
    ui_set_text(s_ov_val[OV_FADE_START], buf);
    ui_fmt_fixed(buf, sizeof(buf), s_overlay.fade_end_mm, 0);
    strcat(buf, " mm");
    ui_set_text(s_ov_val[OV_FADE_END], buf);
}

static void build_options(const ui_preset_list_t *list, uint8_t *slots, uint8_t *count, lv_obj_t *dd)
{
    static char opts[UI_MAX_PRESETS * (UI_PRESET_NAME_LEN + 4)];
    opts[0] = '\0';
    *count = 0;
    for (int i = 0; i < list->count && i < UI_MAX_PRESETS; i++) {
        if (i > 0) {
            strcat(opts, "\n");
        }
        strcat(opts, list->entries[i].name);
        slots[i] = list->entries[i].slot;
        (*count)++;
    }
    lv_dropdown_set_options(dd, *count > 0 ? opts : "(keine)");
}

static int index_of_slot(const uint8_t *slots, uint8_t count, uint8_t slot)
{
    for (int i = 0; i < count; i++) {
        if (slots[i] == slot) {
            return i;
        }
    }
    return -1;
}

/* ---- events ----------------------------------------------------------- */

static void brightness_cb(lv_event_t *e)
{
    int32_t v = lv_slider_get_value(lv_event_get_target(e));
    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%d %%", (int)v);
    ui_set_text(s_brightness_val, buf);
    if (ui_actions.set_brightness) {
        ui_actions.set_brightness(ui_actions.ctx, (uint8_t)v);
    }
}

static void overlay_slider_cb(lv_event_t *e)
{
    ov_slider_t which = (ov_slider_t)(intptr_t)lv_event_get_user_data(e);
    int32_t v = lv_slider_get_value(s_ov_slider[which]);

    switch (which) {
    case OV_LEVEL_START: s_overlay.level_start = (uint8_t)v; break;
    case OV_LEVEL_MID:   s_overlay.level_mid = (uint8_t)v; break;
    case OV_LEVEL_END:   s_overlay.level_end = (uint8_t)v; break;
    case OV_FADE_START:
        /* fades must not overlap: clamp the moved slider */
        if (v + s_overlay.fade_end_mm > s_strip_mm) {
            v = s_strip_mm - s_overlay.fade_end_mm;
            lv_slider_set_value(s_ov_slider[which], v, LV_ANIM_OFF);
        }
        s_overlay.fade_start_mm = (uint16_t)v;
        break;
    case OV_FADE_END:
        if (v + s_overlay.fade_start_mm > s_strip_mm) {
            v = s_strip_mm - s_overlay.fade_start_mm;
            lv_slider_set_value(s_ov_slider[which], v, LV_ANIM_OFF);
        }
        s_overlay.fade_end_mm = (uint16_t)v;
        break;
    default:
        return;
    }
    s_overlay.type = UP_OVERLAY_TYPE_EDGE_FADE;

    update_overlay_labels();
    update_chart();

    if (ui_actions.set_overlay) {
        ui_actions.set_overlay(ui_actions.ctx, &s_overlay);
    }
}

static void pattern_dd_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i < s_pattern_count && ui_actions.select_preset) {
        ui_actions.select_preset(ui_actions.ctx, UI_PRESET_PATTERN, s_pattern_slots[i]);
    }
}

static void overlay_dd_cb(lv_event_t *e)
{
    uint32_t i = lv_dropdown_get_selected(lv_event_get_target(e));
    if (i < s_overlay_count && ui_actions.select_preset) {
        ui_actions.select_preset(ui_actions.ctx, UI_PRESET_OVERLAY, s_overlay_slots[i]);
    }
}

static void editor_cb(lv_event_t *e)
{
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    if (ui_actions.set_editor) {
        ui_actions.set_editor(ui_actions.ctx, on);
    }
}

/* ---- layout ----------------------------------------------------------- */

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

static lv_obj_t *slider(lv_obj_t *parent, int32_t min, int32_t max)
{
    lv_obj_t *s = lv_slider_create(parent);
    lv_slider_set_range(s, min, max);
    lv_obj_set_height(s, 14);
    lv_obj_set_ext_click_area(s, 23);   /* 14 + 2 × 23 = 60 px touch height (I1) */
    lv_obj_set_style_pad_ver(parent, 6, 0);
    return s;
}

static lv_obj_t *dropdown(lv_obj_t *parent, lv_event_cb_t cb)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_obj_set_width(dd, lv_pct(100));
    lv_obj_set_style_text_font(dd, ui_fonts.text, 0);
    lv_obj_set_style_bg_color(dd, UI_COL_BG, 0);
    lv_obj_set_style_border_color(dd, UI_COL_BORDER, 0);
    lv_obj_t *list = lv_dropdown_get_list(dd);
    lv_obj_set_style_text_font(list, ui_fonts.text, 0);
    lv_obj_set_style_max_height(list, 400, 0);
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return dd;
}

static void overlay_row(lv_obj_t *card, ov_slider_t which, const char *name, int32_t max)
{
    lv_obj_t *r = row(card);
    lv_obj_t *l = ui_label(r, ui_fonts.small, UI_COL_TEXT, name);
    lv_obj_set_width(l, 150);
    s_ov_slider[which] = slider(r, 0, max);
    lv_obj_set_flex_grow(s_ov_slider[which], 1);
    lv_obj_add_event_cb(s_ov_slider[which], overlay_slider_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)which);
    s_ov_val[which] = ui_label(r, ui_fonts.small, UI_COL_MUTED, "");
    lv_obj_set_width(s_ov_val[which], 80);
    lv_obj_set_style_text_align(s_ov_val[which], LV_TEXT_ALIGN_RIGHT, 0);
}

void ui_led_create(lv_obj_t *tab)
{
    s_page = tab;

    /* Global brightness */
    lv_obj_t *card = ui_card(tab, NULL);
    lv_obj_t *r = row(card);
    ui_label(r, ui_fonts.small, UI_COL_MUTED, "Helligkeit");
    s_brightness_val = ui_label(r, ui_fonts.text, UI_COL_TEXT, "");
    s_brightness = slider(card, 0, 100);
    lv_obj_set_width(s_brightness, lv_pct(100));
    lv_obj_set_style_pad_bottom(card, 24, 0);   /* room for the slider knob */
    lv_obj_add_event_cb(s_brightness, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Pattern */
    card = ui_card(tab, "Muster");
    s_pattern_dd = dropdown(card, pattern_dd_cb);

    /* Overlay */
    card = ui_card(tab, "Overlay");
    s_overlay_dd = dropdown(card, overlay_dd_cb);
    s_overlay_hint = ui_label(card, ui_fonts.small, UI_COL_MUTED, "");
    lv_obj_set_width(s_overlay_hint, lv_pct(100));
    lv_label_set_long_mode(s_overlay_hint, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_overlay_hint, UI_FLAG_WRAP_ALLOWED);

    s_chart = lv_chart_create(card);
    lv_obj_set_size(s_chart, lv_pct(100), 110);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, CHART_POINTS);
    lv_chart_set_axis_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_div_line_count(s_chart, 3, 0);
    lv_obj_set_style_bg_color(s_chart, UI_COL_BG, 0);
    lv_obj_set_style_border_color(s_chart, UI_COL_BORDER, 0);
    lv_obj_set_style_line_color(s_chart, UI_COL_BORDER, LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);   /* no point markers */
    lv_obj_set_style_line_width(s_chart, 3, LV_PART_ITEMS);
    s_series = lv_chart_add_series(s_chart, UI_COL_ACCENT, LV_CHART_AXIS_PRIMARY_Y);

    overlay_row(card, OV_LEVEL_START, "Anfang", 100);
    overlay_row(card, OV_LEVEL_MID, "Mitte", 100);
    overlay_row(card, OV_LEVEL_END, "Ende", 100);
    overlay_row(card, OV_FADE_START, "Übergang Anf.", 500);
    overlay_row(card, OV_FADE_END, "Übergang Ende", 500);

    /* Preset editor via Wi-Fi */
    card = ui_card(tab, NULL);
    r = row(card);
    ui_label(r, ui_fonts.text, UI_COL_TEXT, LV_SYMBOL_WIFI "  Preset-Editor (WLAN)");
    s_editor_sw = lv_switch_create(r);
    lv_obj_set_ext_click_area(s_editor_sw, 16);
    lv_obj_set_style_bg_color(s_editor_sw, UI_COL_BORDER, LV_PART_MAIN);   /* C1 */
    lv_obj_add_event_cb(s_editor_sw, editor_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_editor_panel = row(card);
    lv_obj_set_flex_align(s_editor_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    s_qr = lv_qrcode_create(s_editor_panel);
    lv_qrcode_set_size(s_qr, 170);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_obj_set_style_border_color(s_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_qr, 6, 0);
    s_wifi_text = ui_label(s_editor_panel, ui_fonts.small, UI_COL_TEXT, "");
    lv_obj_set_flex_grow(s_wifi_text, 1);
    lv_label_set_long_mode(s_wifi_text, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_wifi_text, UI_FLAG_WRAP_ALLOWED);
    lv_obj_add_flag(s_editor_panel, LV_OBJ_FLAG_HIDDEN);
}

void ui_led_update(const ui_model_t *m)
{
    char buf[200];

    s_strip_mm = m->strip_length_mm > 0 ? m->strip_length_mm : 500;

    /* Brightness */
    set_slider(s_brightness, m->global_brightness);
    if (!user_is_touching(s_brightness)) {
        lv_snprintf(buf, sizeof(buf), "%d %%", m->global_brightness);
        ui_set_text(s_brightness_val, buf);
    }

    /* Preset lists */
    if (m->patterns.rev != s_pattern_rev) {
        s_pattern_rev = m->patterns.rev;
        build_options(&m->patterns, s_pattern_slots, &s_pattern_count, s_pattern_dd);
    }
    if (m->overlays.rev != s_overlay_rev) {
        s_overlay_rev = m->overlays.rev;
        build_options(&m->overlays, s_overlay_slots, &s_overlay_count, s_overlay_dd);
        for (int i = 0; i < s_overlay_count; i++) {
            s_overlay_builtin[i] = m->overlays.entries[i].builtin;
        }
    }
    if (!lv_dropdown_is_open(s_pattern_dd)) {
        int i = index_of_slot(s_pattern_slots, s_pattern_count, m->active_pattern);
        if (i >= 0 && (int)lv_dropdown_get_selected(s_pattern_dd) != i) {
            lv_dropdown_set_selected(s_pattern_dd, i);
        }
    }

    /* Overlay: take values from the model unless the user is dragging */
    bool dragging = false;
    for (int i = 0; i < OV_COUNT; i++) {
        dragging |= user_is_touching(s_ov_slider[i]);
    }
    if (!dragging && (!s_overlay_valid || memcmp(&s_overlay, &m->overlay, sizeof(s_overlay)) != 0)) {
        s_overlay = m->overlay;
        s_overlay_valid = true;
        lv_slider_set_range(s_ov_slider[OV_FADE_START], 0, s_strip_mm);
        lv_slider_set_range(s_ov_slider[OV_FADE_END], 0, s_strip_mm);
        set_slider(s_ov_slider[OV_LEVEL_START], s_overlay.level_start);
        set_slider(s_ov_slider[OV_LEVEL_MID], s_overlay.level_mid);
        set_slider(s_ov_slider[OV_LEVEL_END], s_overlay.level_end);
        set_slider(s_ov_slider[OV_FADE_START], s_overlay.fade_start_mm);
        set_slider(s_ov_slider[OV_FADE_END], s_overlay.fade_end_mm);
        update_overlay_labels();
        update_chart();
    }
    int ov = index_of_slot(s_overlay_slots, s_overlay_count, m->active_overlay);
    if (!lv_dropdown_is_open(s_overlay_dd) && ov >= 0 && (int)lv_dropdown_get_selected(s_overlay_dd) != ov) {
        lv_dropdown_set_selected(s_overlay_dd, ov);
    }

    /* Hint below the overlay list: the sliders change the preset itself (ADR 0017) */
    if (m->led_notice == UI_LED_NOTICE_NO_FREE_SLOT) {
        ui_set_text(s_overlay_hint, "Kein freier Platz – im Editor ein Overlay löschen");
        lv_obj_set_style_text_color(s_overlay_hint, UI_COL_WARN, 0);
        lv_obj_remove_flag(s_overlay_hint, LV_OBJ_FLAG_HIDDEN);
    } else if (ov >= 0 && s_overlay_builtin[ov]) {
        ui_set_text(s_overlay_hint, "Eingebaut – Regler legen eine Kopie an");
        lv_obj_set_style_text_color(s_overlay_hint, UI_COL_MUTED, 0);
        lv_obj_remove_flag(s_overlay_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_overlay_hint, LV_OBJ_FLAG_HIDDEN);
    }

    /* Editor */
    if (!user_is_touching(s_editor_sw)) {
        if (m->editor_on) {
            lv_obj_add_state(s_editor_sw, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(s_editor_sw, LV_STATE_CHECKED);
        }
    }
    if (m->editor_on) {
        lv_obj_remove_flag(s_editor_panel, LV_OBJ_FLAG_HIDDEN);
        char qr[128];
        lv_snprintf(qr, sizeof(qr), "WIFI:T:WPA;S:%s;P:%s;;", m->wifi_ssid, m->wifi_password);
        if (strcmp(qr, s_qr_data) != 0) {
            strcpy(s_qr_data, qr);
            lv_qrcode_update(s_qr, s_qr_data, strlen(s_qr_data));
        }
        lv_snprintf(buf, sizeof(buf), "Netz:\n%s\n\nPasswort:\n%s\n\nAdresse:\n%s",
                    m->wifi_ssid, m->wifi_password, m->editor_url);
        ui_set_text(s_wifi_text, buf);
    } else {
        lv_obj_add_flag(s_editor_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_led_scroll(bool to_bottom)
{
    lv_obj_update_layout(s_page);
    if (to_bottom) {
        lv_obj_scroll_to_y(s_page, LV_COORD_MAX, LV_ANIM_OFF);
    } else {
        lv_obj_scroll_to_y(s_page, 0, LV_ANIM_OFF);
    }
}
