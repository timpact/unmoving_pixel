/* Settings screen: display brightness, measurement resolution, encoder calibration (ADR 0010). */
#include <stdio.h>

#include "ui_internal.h"

static lv_obj_t *s_disp_slider;
static lv_obj_t *s_disp_val;
static lv_obj_t *s_resolution;

static lv_obj_t *row(lv_obj_t *parent)
{
    lv_obj_t *r = lv_obj_create(parent);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

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
    card = ui_card(tab, "Auflösung Messstrecke");
    s_resolution = lv_dropdown_create(card);
    lv_obj_set_width(s_resolution, lv_pct(100));
    lv_dropdown_set_options(s_resolution, "1 m\n10 cm\n1 cm");
    lv_obj_set_style_bg_color(s_resolution, UI_COL_BG, 0);
    lv_obj_set_style_border_color(s_resolution, UI_COL_BORDER, 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(s_resolution), ui_fonts.text, 0);
    lv_obj_add_event_cb(s_resolution, resolution_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Encoder calibration: concept pending (ADR to be defined) */
    card = ui_card(tab, "Drehgeber-Kalibrierung");
    lv_obj_t *l = ui_label(card, ui_fonts.small, UI_COL_MUTED,
                           "Wird nach Festlegung im ADR ergänzt\n(Messfahrt über eine bekannte Strecke).");
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
}

void ui_settings_update(const ui_model_t *m)
{
    char buf[16];
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
}
