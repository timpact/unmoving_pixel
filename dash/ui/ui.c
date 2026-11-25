/* dash GUI: setup, tab view and shared helpers (ADR 0010). */
#include <stdio.h>
#include <string.h>

#include "ui_internal.h"

ui_fonts_t ui_fonts;
ui_actions_t ui_actions;

static lv_obj_t *s_tabview;

static lv_font_t s_text_fonts[3];

/* Large font: Tiny TTF only */
static const lv_font_t *make_ttf(const void *data, size_t size, int32_t px)
{
    lv_font_t *f = lv_tiny_ttf_create_data(data, size, px);
    if (f == NULL) {
        LV_LOG_ERROR("tiny_ttf: could not create font %d px", (int)px);
        return &lv_font_montserrat_34;
    }
    return f;
}

/* Text font: built-in Montserrat (ASCII + icons) with Tiny TTF fallback (umlauts) */
static const lv_font_t *make_text_font(lv_font_t *storage, const lv_font_t *builtin,
                                       const void *data, size_t size, int32_t px)
{
    *storage = *builtin;   /* built-in fonts are const: use a copy to set the fallback */
    storage->fallback = lv_tiny_ttf_create_data(data, size, px);
    return storage;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

void ui_set_text(lv_obj_t *label, const char *text)
{
    if (strcmp(lv_label_get_text(label), text) != 0) {
        lv_label_set_text(label, text);
    }
}

lv_obj_t *ui_card(lv_obj_t *parent, const char *title)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, UI_COL_CARD, 0);
    lv_obj_set_style_border_color(card, UI_COL_BORDER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_style_pad_row(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    if (title != NULL) {
        ui_label(card, ui_fonts.small, UI_COL_MUTED, title);
    }
    return card;
}

void ui_fixed_num_create(ui_fixed_num_t *n, lv_obj_t *parent, int32_t width, int32_t comma_x,
                         const lv_font_t *int_font, lv_color_t int_color,
                         const lv_font_t *rest_font, lv_color_t rest_color)
{
    const int32_t h = lv_font_get_line_height(int_font);

    n->box = lv_obj_create(parent);
    lv_obj_remove_style_all(n->box);
    lv_obj_set_size(n->box, width, h);
    lv_obj_remove_flag(n->box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(n->box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    n->int_part = ui_label(n->box, int_font, int_color, "");
    lv_obj_set_width(n->int_part, comma_x);
    lv_obj_set_style_text_align(n->int_part, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(n->int_part, 0, 0);

    /* Decimals and unit: bottom-aligned row, padded so that all baselines match */
    lv_obj_t *tail = lv_obj_create(n->box);
    lv_obj_remove_style_all(tail);
    lv_obj_set_size(tail, 240, h);
    lv_obj_set_pos(tail, comma_x, 0);
    lv_obj_add_flag(tail, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_remove_flag(tail, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(tail, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tail, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(tail, 6, 0);

    n->rest = ui_label(tail, rest_font, rest_color, "");
    lv_obj_set_style_pad_bottom(n->rest, int_font->base_line - rest_font->base_line, 0);

    n->unit = ui_label(tail, ui_fonts.small, UI_COL_MUTED, "");
    lv_obj_set_style_pad_bottom(n->unit, int_font->base_line - ui_fonts.small->base_line, 0);
}

void ui_fixed_num_set(ui_fixed_num_t *n, const char *text, const char *unit)
{
    char ip[32];
    const char *comma = strchr(text, ',');
    size_t len = comma ? (size_t)(comma - text) : strlen(text);
    if (len >= sizeof(ip)) {
        len = sizeof(ip) - 1;
    }
    memcpy(ip, text, len);
    ip[len] = '\0';
    ui_set_text(n->int_part, ip);
    ui_set_text(n->rest, comma ? comma : "");
    ui_set_text(n->unit, unit ? unit : "");
}

void ui_fmt_fixed(char *buf, size_t len, int64_t value, int decimals)
{
    char digits[32];
    bool neg = value < 0;
    uint64_t v = neg ? (uint64_t)(-value) : (uint64_t)value;

    uint64_t scale = 1;
    for (int i = 0; i < decimals; i++) {
        scale *= 10;
    }
    uint64_t ip = v / scale;
    uint64_t fp = v % scale;

    /* integer part with thousands separators */
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%llu", (unsigned long long)ip);
    size_t n = strlen(tmp);
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (i > 0 && (n - i) % 3 == 0) {
            digits[o++] = '.';
        }
        digits[o++] = tmp[i];
    }
    digits[o] = '\0';

    if (decimals > 0) {
        snprintf(buf, len, "%s%s,%0*llu", neg ? "-" : "", digits, decimals, (unsigned long long)fp);
    } else {
        snprintf(buf, len, "%s%s", neg ? "-" : "", digits);
    }
}

void ui_init(const ui_actions_t *actions, const void *font_data, size_t font_size)
{
    ui_actions = *actions;

    ui_fonts.small = make_text_font(&s_text_fonts[0], &lv_font_montserrat_18, font_data, font_size, 18);
    ui_fonts.text = make_text_font(&s_text_fonts[1], &lv_font_montserrat_22, font_data, font_size, 22);
    ui_fonts.value = make_text_font(&s_text_fonts[2], &lv_font_montserrat_34, font_data, font_size, 34);
    ui_fonts.measure = make_ttf(font_data, font_size, 42);
    ui_fonts.speed = make_ttf(font_data, font_size, 120);
    ui_fonts.speed_dec = make_ttf(font_data, font_size, 72);

    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, UI_COL_ACCENT, UI_COL_WARN, true, ui_fonts.text);
    lv_display_set_theme(disp, th);

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, UI_COL_BG, 0);
    lv_obj_set_style_text_font(scr, ui_fonts.text, 0);

    s_tabview = lv_tabview_create(scr);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_BOTTOM);
    lv_tabview_set_tab_bar_size(s_tabview, 72);
    lv_obj_set_style_bg_color(s_tabview, UI_COL_BG, 0);

    lv_obj_t *bar = lv_tabview_get_tab_bar(s_tabview);
    lv_obj_set_style_bg_color(bar, UI_COL_CARD, 0);
    lv_obj_set_style_text_font(bar, ui_fonts.small, 0);

    /* Icon above text */
    lv_obj_t *tabs[3];
    tabs[UI_TAB_RIDE] = lv_tabview_add_tab(s_tabview, LV_SYMBOL_HOME "\nFahrt");
    tabs[UI_TAB_LED] = lv_tabview_add_tab(s_tabview, LV_SYMBOL_IMAGE "\nLED");
    tabs[UI_TAB_SETTINGS] = lv_tabview_add_tab(s_tabview, LV_SYMBOL_SETTINGS "\nEinstellungen");

    for (uint32_t i = 0; i < 3; i++) {
        lv_obj_t *btn = lv_obj_get_child(bar, (int32_t)i);
        lv_obj_t *label = lv_obj_get_child(btn, 0);
        if (label != NULL) {
            lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_line_space(label, 4, 0);
        }
        lv_obj_set_style_pad_all(tabs[i], 16, 0);
        lv_obj_set_style_pad_row(tabs[i], 12, 0);
        lv_obj_set_flex_flow(tabs[i], LV_FLEX_FLOW_COLUMN);
    }

    ui_ride_create(tabs[UI_TAB_RIDE]);
    ui_led_create(tabs[UI_TAB_LED]);
    ui_settings_create(tabs[UI_TAB_SETTINGS]);
}

void ui_update(const ui_model_t *model)
{
    ui_ride_update(model);
    ui_led_update(model);
    ui_settings_update(model);
}

void ui_show_tab(uint32_t index)
{
    lv_tabview_set_active(s_tabview, index, LV_ANIM_OFF);
}

void ui_scroll_led_page(bool to_bottom)
{
    ui_led_scroll(to_bottom);
}
