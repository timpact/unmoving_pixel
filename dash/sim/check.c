/*
 * Automated GUI checks (ADR 0016).
 *
 * Renders defined scenarios with fixed test data and checks the measurable rules
 * of docs/ui-guidelines.md:
 *   N2/N3/N7  layout stability over a sweep of values (comma, unit, no shifts)
 *   T2/L6     text fits: no unexpected wrapping, no clipping, no overlapping text
 *   L3        ride screen fits without scrolling
 *   I1–I3     touch target size and spacing
 *   C1        colour tokens
 *   REF       pixel comparison with approved reference screenshots
 */
#include "check.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "headless.h"
#include "lvgl.h"
#include "png.h"
#include "ui.h"

#ifndef SIM_REFERENCE_DIR
#define SIM_REFERENCE_DIR "reference"
#endif
#ifndef SIM_CHECK_OUT_DIR
#define SIM_CHECK_OUT_DIR "check"
#endif

extern const unsigned char ui_font_data[];
extern const unsigned long ui_font_data_size;

#define MIN_TOUCH      60   /* I1 */
#define MIN_TOUCH_RIDE 64   /* I2 */
#define MIN_GAP        10   /* I3 */

static int s_failures;
static int s_checks;
static const char *s_scenario = "";
static ui_model_t s_m;

/* ---- reporting ------------------------------------------------------------ */

static void fail(const char *rule, const char *fmt, ...)
{
    va_list ap;
    printf("FAIL  %-5s %-24s ", rule, s_scenario);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    s_failures++;
}

/* ---- object helpers ------------------------------------------------------- */

/* Intersection of two areas; returns false if they do not overlap */
static bool area_intersect(lv_area_t *res, const lv_area_t *a, const lv_area_t *b)
{
    lv_area_t r = {LV_MAX(a->x1, b->x1), LV_MAX(a->y1, b->y1), LV_MIN(a->x2, b->x2), LV_MIN(a->y2, b->y2)};
    *res = r;
    return r.x1 <= r.x2 && r.y1 <= r.y2;
}

static bool is_type(lv_obj_t *o, const lv_obj_class_t *c)
{
    return lv_obj_check_type(o, c);
}

static const char *kind(lv_obj_t *o)
{
    if (is_type(o, &lv_label_class)) return "label";
    if (is_type(o, &lv_button_class)) return "button";
    if (is_type(o, &lv_slider_class)) return "slider";
    if (is_type(o, &lv_switch_class)) return "switch";
    if (is_type(o, &lv_dropdown_class)) return "dropdown";
    if (is_type(o, &lv_textarea_class)) return "textarea";
    if (is_type(o, &lv_roller_class)) return "roller";
    if (is_type(o, &lv_keyboard_class)) return "keyboard";
    if (is_type(o, &lv_arc_class)) return "arc";
    if (is_type(o, &lv_chart_class)) return "chart";
    if (is_type(o, &lv_qrcode_class)) return "qrcode";
    return "obj";
}

/* Text that identifies an object in messages */
static const char *ident(lv_obj_t *o)
{
    static char buf[64];
    const char *t = "";
    if (is_type(o, &lv_label_class)) {
        t = lv_label_get_text(o);
    } else if (lv_obj_get_child_count(o) > 0 && is_type(lv_obj_get_child(o, 0), &lv_label_class)) {
        t = lv_label_get_text(lv_obj_get_child(o, 0));
    }
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    char tt[28];
    snprintf(tt, sizeof(tt), "%s", t);
    for (char *p = tt; *p; p++) {
        if (*p == '\n') *p = ' ';
    }
    snprintf(buf, sizeof(buf), "%s \"%s\" @%d,%d %dx%d", kind(o), tt, (int)a.x1, (int)a.y1,
             (int)lv_area_get_width(&a), (int)lv_area_get_height(&a));
    return buf;
}

static bool has_ancestor(lv_obj_t *o, lv_obj_t *anc)
{
    for (lv_obj_t *p = lv_obj_get_parent(o); p != NULL; p = lv_obj_get_parent(p)) {
        if (p == anc) return true;
    }
    return false;
}

/* Labels and buttons inside composite widgets are the widget's own business */
static bool inside_widget(lv_obj_t *o)
{
    for (lv_obj_t *p = lv_obj_get_parent(o); p != NULL; p = lv_obj_get_parent(p)) {
        if (is_type(p, &lv_dropdown_class) || is_type(p, &lv_roller_class) ||
            is_type(p, &lv_textarea_class) || is_type(p, &lv_keyboard_class) ||
            is_type(p, &lv_chart_class)) {
            return true;
        }
    }
    return false;
}

static bool dialog_open(void)
{
    lv_obj_t *top = lv_layer_top();
    for (uint32_t i = 0; i < lv_obj_get_child_count(top); i++) {
        if (!lv_obj_has_flag(lv_obj_get_child(top, (int32_t)i), LV_OBJ_FLAG_HIDDEN)) {
            return true;
        }
    }
    return false;
}

/* Root whose content is in front: the dialog layer if a dialog is open */
static lv_obj_t *active_root(void)
{
    return dialog_open() ? lv_layer_top() : lv_screen_active();
}

typedef void (*visit_cb_t)(lv_obj_t *o, void *ctx);

static void walk(lv_obj_t *o, visit_cb_t cb, void *ctx)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    cb(o, ctx);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
        walk(lv_obj_get_child(o, (int32_t)i), cb, ctx);
    }
}

static bool is_tab(lv_obj_t *o)
{
    for (uint32_t i = 0; i < 3; i++) {
        if (ui_get_tab(i) == o) return true;
    }
    return false;
}

/* Visible part of an object: clipped by the pages (tabs) it lives in and the screen */
static bool visible_area(lv_obj_t *o, lv_area_t *out)
{
    lv_obj_get_coords(o, out);
    for (lv_obj_t *p = lv_obj_get_parent(o); p != NULL; p = lv_obj_get_parent(p)) {
        if (is_tab(p) || lv_obj_get_parent(p) == NULL) {
            lv_area_t pa;
            lv_obj_get_coords(p, &pa);
            if (!area_intersect(out, out, &pa)) {
                return false;
            }
        }
    }
    return true;
}

/* Bounding box of the rendered text of a label */
static void text_box(lv_obj_t *l, lv_area_t *box)
{
    const char *t = lv_label_get_text(l);
    const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    lv_area_t c;
    lv_obj_get_content_coords(l, &c);
    lv_point_t sz;
    lv_text_get_size(&sz, t, f, lv_obj_get_style_text_letter_space(l, LV_PART_MAIN),
                     lv_obj_get_style_text_line_space(l, LV_PART_MAIN), lv_area_get_width(&c),
                     LV_TEXT_FLAG_NONE);
    lv_text_align_t al = lv_obj_get_style_text_align(l, LV_PART_MAIN);
    box->y1 = c.y1;
    box->y2 = c.y1 + sz.y - 1;
    if (al == LV_TEXT_ALIGN_RIGHT) {
        box->x2 = c.x2;
        box->x1 = c.x2 - sz.x + 1;
    } else if (al == LV_TEXT_ALIGN_CENTER) {
        box->x1 = c.x1 + (lv_area_get_width(&c) - sz.x) / 2;
        box->x2 = box->x1 + sz.x - 1;
    } else {
        box->x1 = c.x1;
        box->x2 = c.x1 + sz.x - 1;
    }
}

/* ---- T2 / L6: text fits ---------------------------------------------------- */

#define MAX_LABELS 512
static lv_obj_t *s_labels[MAX_LABELS];
static int s_label_count;

static void collect_label(lv_obj_t *o, void *ctx)
{
    (void)ctx;
    if (is_type(o, &lv_label_class) && !inside_widget(o) && lv_label_get_text(o)[0] != '\0' &&
        s_label_count < MAX_LABELS) {
        s_labels[s_label_count++] = o;
    }
}

/* Page (tab) an object lives in, or NULL */
static lv_obj_t *page_of(lv_obj_t *o)
{
    for (lv_obj_t *p = lv_obj_get_parent(o); p != NULL; p = lv_obj_get_parent(p)) {
        if (is_tab(p)) return p;
    }
    return NULL;
}

static void check_label(lv_obj_t *l)
{
    const char *text = lv_label_get_text(l);
    lv_area_t va;
    if (!visible_area(l, &va)) {
        return;   /* on another tab or scrolled out of view */
    }
    const lv_font_t *f = lv_obj_get_style_text_font(l, LV_PART_MAIN);
    int32_t lh = lv_font_get_line_height(f);
    s_checks++;

    /* Unexpected wrapping */
    if (!lv_obj_has_flag(l, UI_FLAG_WRAP_ALLOWED) && strchr(text, '\n') == NULL &&
        lv_obj_get_content_height(l) > lh * 3 / 2) {
        fail("T2", "text wraps: %s", ident(l));
        return;
    }

    /* Clipping by an ancestor (pages scroll vertically, the tab view horizontally).
     * Labels only partly scrolled into view are skipped. */
    lv_area_t tb, la, pg;
    text_box(l, &tb);
    lv_obj_get_coords(l, &la);
    lv_obj_t *page = page_of(l);
    if (page != NULL) {
        lv_obj_get_coords(page, &pg);
        if (la.y1 < pg.y1 || la.y2 > pg.y2) {
            return;
        }
    }
    for (lv_obj_t *p = lv_obj_get_parent(l); p != NULL; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) {
            continue;
        }
        lv_area_t pa;
        lv_obj_get_coords(p, &pa);
        bool chk_x = true, chk_y = true;
        if (is_tab(p)) {
            chk_y = false;                       /* page scrolls vertically */
        } else if (lv_obj_get_child_count(p) > 0 && is_tab(lv_obj_get_child(p, 0))) {
            chk_x = false;                       /* tab view content scrolls horizontally */
        }
        if ((chk_x && (tb.x1 < pa.x1 || tb.x2 > pa.x2)) || (chk_y && (tb.y1 < pa.y1 || tb.y2 > pa.y2))) {
            fail(lv_obj_get_parent(p) == NULL ? "L6" : "T2", "text cut off by its container: %s", ident(l));
            return;
        }
    }
}

static void check_texts(void)
{
    s_label_count = 0;
    walk(active_root(), collect_label, NULL);
    for (int i = 0; i < s_label_count; i++) {
        check_label(s_labels[i]);
    }
    /* Overlapping text (e.g. decimals running into the unit). Only the visible parts
       count: a label cut off at the edge of a scrolled page is hidden behind it. */
    for (int i = 0; i < s_label_count; i++) {
        lv_area_t a, va, ta;
        if (!visible_area(s_labels[i], &va)) continue;
        text_box(s_labels[i], &ta);
        if (!area_intersect(&a, &ta, &va)) continue;
        for (int j = i + 1; j < s_label_count; j++) {
            lv_area_t b, vb, tb, x;
            if (!visible_area(s_labels[j], &vb)) continue;
            text_box(s_labels[j], &tb);
            if (!area_intersect(&b, &tb, &vb)) continue;
            if (area_intersect(&x, &a, &b) && lv_area_get_width(&x) > 1 && lv_area_get_height(&x) > 1) {
                char first[64];
                snprintf(first, sizeof(first), "%s", ident(s_labels[i]));
                fail("T2", "texts overlap: %s and %s", first, ident(s_labels[j]));
            }
        }
    }
}

/* ---- L3: ride screen without scrolling -------------------------------------- */

static void check_ride_fits(void)
{
    lv_obj_t *tab = ui_get_tab(UI_TAB_RIDE);
    lv_obj_update_layout(tab);
    s_checks++;
    if (lv_obj_get_scroll_bottom(tab) > 0) {
        fail("L3", "ride screen needs scrolling (%d px too high)", (int)lv_obj_get_scroll_bottom(tab));
    }
}

/* ---- I1–I3: touch targets ------------------------------------------------------ */

#define MAX_TARGETS 128
static lv_obj_t *s_targets[MAX_TARGETS];
static int s_target_count;

static bool is_target(lv_obj_t *o)
{
    return is_type(o, &lv_button_class) || is_type(o, &lv_slider_class) || is_type(o, &lv_switch_class) ||
           is_type(o, &lv_dropdown_class) || is_type(o, &lv_textarea_class) || is_type(o, &lv_roller_class);
}

static void collect_target(lv_obj_t *o, void *ctx)
{
    (void)ctx;
    lv_area_t va;
    if (is_target(o) && !inside_widget(o) && visible_area(o, &va) && s_target_count < MAX_TARGETS) {
        s_targets[s_target_count++] = o;
    }
}

/* Buttons of the tab bar form one segmented control (no gap needed between them) */
static bool in_tab_bar(lv_obj_t *o)
{
    lv_obj_t *bar = lv_obj_get_parent(o);
    lv_obj_t *tv = bar ? lv_obj_get_parent(bar) : NULL;
    return tv != NULL && is_type(tv, &lv_tabview_class) && bar == lv_tabview_get_tab_bar(tv);
}

static bool clipped(lv_obj_t *o, const lv_area_t *vis)
{
    lv_area_t full;
    lv_obj_get_coords(o, &full);
    return vis->x1 != full.x1 || vis->y1 != full.y1 || vis->x2 != full.x2 || vis->y2 != full.y2;
}

static void check_targets(void)
{
    s_target_count = 0;
    walk(active_root(), collect_target, NULL);
    for (int i = 0; i < s_target_count; i++) {
        lv_obj_t *o = s_targets[i];
        lv_area_t click, vis;
        lv_obj_get_click_area(o, &click);
        lv_obj_get_coords(o, &vis);
        s_checks++;
        if (lv_area_get_width(&click) < MIN_TOUCH || lv_area_get_height(&click) < MIN_TOUCH) {
            fail("I1", "touch target smaller than %d px: %s (click area %dx%d)", MIN_TOUCH, ident(o),
                 (int)lv_area_get_width(&click), (int)lv_area_get_height(&click));
        }
        if (has_ancestor(o, ui_get_tab(UI_TAB_RIDE)) && lv_area_get_height(&vis) < MIN_TOUCH_RIDE) {
            fail("I2", "control on the ride screen lower than %d px: %s", MIN_TOUCH_RIDE, ident(o));
        }
    }
    for (int i = 0; i < s_target_count; i++) {
        for (int j = i + 1; j < s_target_count; j++) {
            lv_obj_t *a = s_targets[i], *b = s_targets[j];
            if (has_ancestor(a, b) || has_ancestor(b, a) || (in_tab_bar(a) && in_tab_bar(b))) {
                continue;
            }
            lv_area_t ra, rb;
            if (!visible_area(a, &ra) || !visible_area(b, &rb)) continue;
            /* a target cut off at the edge of a scrolled page touches whatever lies
               beyond that edge (e.g. the tab bar); it is checked when scrolled into view */
            if (clipped(a, &ra) || clipped(b, &rb)) continue;
            int32_t dx = LV_MAX(ra.x1 - rb.x2, rb.x1 - ra.x2) - 1;
            int32_t dy = LV_MAX(ra.y1 - rb.y2, rb.y1 - ra.y2) - 1;
            int32_t gap = LV_MAX(dx, dy);
            s_checks++;
            if (gap < MIN_GAP) {
                char first[64];
                snprintf(first, sizeof(first), "%s", ident(a));
                fail("I3", "touch targets %d px apart: %s and %s", (int)gap, first, ident(b));
            }
        }
    }
}

/* ---- C1: colour tokens -------------------------------------------------------- */

static const uint32_t k_tokens[] = {0x0d1117, 0x161b22, 0x30363d, 0xe6edf3, 0x8b949e,
                                    0x2dd4bf, 0x22c55e, 0xf59e0b, 0xef4444};

static bool is_token(lv_color_t c)
{
    uint32_t v = lv_color_to_u32(c) & 0xFFFFFF;
    for (size_t i = 0; i < sizeof(k_tokens) / sizeof(k_tokens[0]); i++) {
        if (v == k_tokens[i]) return true;
    }
    return false;
}

static void check_colour(lv_obj_t *o, void *ctx)
{
    (void)ctx;
    if (inside_widget(o) || is_type(o, &lv_qrcode_class)) {
        return;   /* QR code is black on white by design */
    }
    s_checks++;
    if (is_type(o, &lv_label_class)) {
        lv_color_t c = lv_obj_get_style_text_color(o, LV_PART_MAIN);
        if (lv_label_get_text(o)[0] != '\0' && !is_token(c)) {
            fail("C1", "text colour #%06x is no token: %s", (unsigned)(lv_color_to_u32(c) & 0xFFFFFF), ident(o));
        }
    }
    if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) > LV_OPA_TRANSP && o != lv_layer_top()) {
        lv_color_t c = lv_obj_get_style_bg_color(o, LV_PART_MAIN);
        bool dim_layer = lv_obj_get_parent(o) == lv_layer_top() && (lv_color_to_u32(c) & 0xFFFFFF) == 0;
        if (!is_token(c) && !dim_layer) {
            fail("C1", "background #%06x is no token: %s", (unsigned)(lv_color_to_u32(c) & 0xFFFFFF), ident(o));
        }
    }
}

/* ---- REF: reference screenshots ----------------------------------------------- */

static bool s_update_refs;
static bool s_refs_enabled;   /* reference comparison only once references exist (ADR 0016) */
static int s_refs_skipped;
static uint8_t s_rgb[HOR_RES * VER_RES * 3];

static void check_reference(const char *name)
{
    char out[512], ref[512], diff[512];
    snprintf(out, sizeof(out), "%s/%s.png", SIM_CHECK_OUT_DIR, name);
    snprintf(ref, sizeof(ref), "%s/%s.png", SIM_REFERENCE_DIR, name);
    snprintf(diff, sizeof(diff), "%s/%s_diff.png", SIM_CHECK_OUT_DIR, name);

    headless_render();
    headless_get_rgb(s_rgb);
    png_write_rgb(out, HOR_RES, VER_RES, s_rgb);
    remove(diff);

    if (!s_update_refs && !s_refs_enabled) {
        s_refs_skipped++;
        return;
    }
    if (s_update_refs) {
        if (png_write_rgb(ref, HOR_RES, VER_RES, s_rgb) != 0) {
            fail("REF", "could not write %s", ref);
        }
        return;
    }
    s_checks++;
    int w, h;
    uint8_t *r = NULL;
    if (png_read_rgb(ref, &w, &h, &r) != 0 || w != HOR_RES || h != VER_RES) {
        fail("REF", "no reference image %s (run with --update-references after review)", ref);
        free(r);
        return;
    }
    int differ = 0;
    static uint8_t d[HOR_RES * VER_RES * 3];
    for (int i = 0; i < HOR_RES * VER_RES; i++) {
        bool same = memcmp(&s_rgb[i * 3], &r[i * 3], 3) == 0;
        differ += !same;
        d[i * 3 + 0] = same ? s_rgb[i * 3 + 0] / 4 : 255;   /* differences in red */
        d[i * 3 + 1] = same ? s_rgb[i * 3 + 1] / 4 : 0;
        d[i * 3 + 2] = same ? s_rgb[i * 3 + 2] / 4 : 0;
    }
    free(r);
    if (differ > 0) {
        png_write_rgb(diff, HOR_RES, VER_RES, d);
        fail("REF", "%d pixels differ from the reference (see %s)", differ, diff);
    }
}

/* ---- scenario runner ------------------------------------------------------------ */

static void apply(void)
{
    ui_update(&s_m);
    headless_run(100);
    lv_obj_update_layout(lv_screen_active());
    lv_obj_update_layout(lv_layer_top());
}

static void run_scenario(const char *name)
{
    s_scenario = name;
    apply();
    headless_render();
    check_texts();
    check_targets();
    walk(active_root(), check_colour, NULL);
    if (!dialog_open() && lv_obj_is_visible(ui_get_tab(UI_TAB_RIDE))) {
        check_ride_fits();
    }
    check_reference(name);
}

static void show(uint32_t tab, bool bottom)
{
    ui_show_tab(tab);
    lv_obj_update_layout(lv_screen_active());
    ui_scroll_page(bottom);
}

/* ---- test data ------------------------------------------------------------------ */

static void preset(ui_preset_list_t *l, uint8_t slot, bool builtin, const char *name)
{
    ui_preset_entry_t *e = &l->entries[l->count++];
    e->slot = slot;
    e->builtin = builtin;
    snprintf(e->name, sizeof(e->name), "%s", name);
}

static void history(int n)
{
    s_m.calib_history_count = (uint8_t)n;
    for (int i = 0; i < n; i++) {
        ui_calib_entry_t *c = &s_m.calib_history[i];
        c->timestamp = i == n - 1 ? 0 : 1795000000u - (uint32_t)i * 3000000u;
        c->total_m = 1234000u - (uint32_t)i * 98000u;
        c->ref_cm = 1000u + (uint32_t)i * 250u;
        c->nm_per_count = 26180u + (uint32_t)((i * 137) % 600);
        c->counts = c->ref_cm * 10000000u / c->nm_per_count;
    }
}

static void base_model(void)
{
    memset(&s_m, 0, sizeof(s_m));
    s_m.link_ok = true;
    s_m.speed_mm_s = 3444;
    s_m.trip_m = 12390;
    s_m.total_m = 1234567;
    s_m.battery_mv = 7420;
    s_m.measure_mm[0] = 51320;
    s_m.measure_mm[1] = 26840;
    s_m.measure_running[0] = true;
    s_m.measure_running[1] = true;
    s_m.display_brightness = 70;
    s_m.measure_decimals = 2;
    s_m.global_brightness = 80;
    s_m.overlay = (up_overlay_t){UP_OVERLAY_TYPE_EDGE_FADE, 10, 100, 10, 150, 150};
    s_m.active_pattern = 0;
    s_m.active_overlay = 1;
    s_m.strip_length_mm = 500;
    s_m.main_leds = UP_STRIP_MAIN_DEF;
    s_m.front_leds = UP_STRIP_END_DEF;
    s_m.rear_leds = UP_STRIP_END_DEF;
    s_m.layout_flags = UP_LAYOUT_LEFT_DATA_FRONT | UP_LAYOUT_RIGHT_DATA_FRONT;
    s_m.light_brightness = 60;
    s_m.brake_enabled = true;
    s_m.brake_threshold = 10;
    s_m.patterns.rev = 1;
    preset(&s_m.patterns, 0, true, "Streifen");
    preset(&s_m.patterns, 1, true, "Punkte");
    preset(&s_m.patterns, 8, false, "10-m-Muster");
    s_m.overlays.rev = 1;
    preset(&s_m.overlays, 0, true, "Ohne");
    preset(&s_m.overlays, 1, true, "Weiche Ränder");
    preset(&s_m.overlays, 8, false, "Mein Overlay");
    s_m.nm_per_count = 26500;
    history(3);
    s_m.time_valid = true;
    s_m.time_now = 1795003600u;
    snprintf(s_m.wifi_ssid, sizeof(s_m.wifi_ssid), "unmoving-pixel");
    snprintf(s_m.wifi_password, sizeof(s_m.wifi_password), "k7Fq-29xa-Lm4d");
    snprintf(s_m.editor_url, sizeof(s_m.editor_url), "http://192.168.4.1");
}

/* ---- N2 / N3 / N7: layout stability ---------------------------------------------- */

#define MAX_OBJS 256
typedef struct {
    lv_obj_t *o;
    lv_area_t a;
    int32_t comma_x;   /* absolute x of the decimal comma, -1 if none */
    bool label;
} geo_t;

static geo_t s_geo0[MAX_OBJS];
static geo_t s_geo[MAX_OBJS];
static int s_geo_n;

static void collect_geo(lv_obj_t *o, void *ctx)
{
    geo_t *g = ctx;
    if (s_geo_n >= MAX_OBJS || inside_widget(o)) return;
    geo_t *e = &g[s_geo_n++];
    e->o = o;
    lv_obj_get_coords(o, &e->a);
    e->label = is_type(o, &lv_label_class);
    e->comma_x = -1;
    if (e->label) {
        const char *t = lv_label_get_text(o);
        uint32_t letter = 0;
        for (const char *p = t; *p; p++) {
            if (*p == ',') {
                lv_point_t pos;
                lv_label_get_letter_pos(o, letter, &pos);
                e->comma_x = e->a.x1 + pos.x;
                break;
            }
            if (((unsigned char)*p & 0xC0) != 0x80) letter++;
        }
    }
}

static int collect_ride_geo(geo_t *g)
{
    s_geo_n = 0;
    walk(ui_get_tab(UI_TAB_RIDE), collect_geo, g);
    return s_geo_n;
}

static void sweep_sample(int idx, int n0)
{
    char name[32];
    snprintf(name, sizeof(name), "sweep #%d", idx);
    s_scenario = name;
    apply();
    check_texts();
    int n = collect_ride_geo(s_geo);
    for (int i = 0; i < n; i++) {
        geo_t *e = &s_geo[i];
        for (int j = 0; j < n0; j++) {
            geo_t *r = &s_geo0[j];
            if (r->o != e->o) continue;
            s_checks++;
            if (!e->label) {
                if (memcmp(&e->a, &r->a, sizeof(lv_area_t)) != 0) {
                    fail("N7", "object moved: %s", ident(e->o));
                }
            } else {
                int32_t dc = (e->a.x1 + e->a.x2) - (r->a.x1 + r->a.x2);   /* 2 × centre shift */
                bool x_ok = e->a.x1 == r->a.x1 || e->a.x2 == r->a.x2 || (dc >= -2 && dc <= 2);
                if (!x_ok || e->a.y1 != r->a.y1) {
                    fail("N3", "text moved (unit or label): %s", ident(e->o));
                }
                if (e->comma_x >= 0 && r->comma_x >= 0 && e->comma_x != r->comma_x) {
                    fail("N2", "decimal comma moved by %d px: %s", (int)(e->comma_x - r->comma_x), ident(e->o));
                }
            }
            break;
        }
    }
}

static void layout_sweep(void)
{
    static const int16_t speeds[] = {0, 49, 1111, 1234, 2777, 3999, 4444, 8888, -300, -1500};
    static const int32_t meas[][2] = {
        {0, 0}, {5, -5}, {12345, -12345}, {99999990, -99999990}, {88888880, 11111110},
        {44444440, -7777770}, {123456789, -123456789}, {100000000, 999999999}};
    static const uint32_t dist[][2] = {{0, 0}, {999, 9999}, {11111, 111111}, {88888, 8888888},
                                       {99999, 9999999}, {44444, 4444444}};

    base_model();
    show(UI_TAB_RIDE, false);
    apply();
    s_geo_n = 0;
    int n0 = collect_ride_geo(s_geo0);

    int idx = 0;
    for (uint8_t dec = 0; dec <= 2; dec++) {
        for (size_t s = 0; s < sizeof(speeds) / sizeof(speeds[0]); s++) {
            for (size_t k = 0; k < sizeof(meas) / sizeof(meas[0]); k++) {
                base_model();
                s_m.measure_decimals = dec;
                s_m.speed_mm_s = speeds[s];
                s_m.measure_mm[0] = meas[k][0];
                s_m.measure_mm[1] = meas[k][1];
                s_m.measure_running[0] = (k & 1) != 0;
                s_m.trip_m = dist[k % 6][0];
                s_m.total_m = dist[k % 6][1];
                s_m.link_ok = !(s == 0 && k == 3);
                sweep_sample(idx++, n0);
            }
        }
    }
    printf("      layout sweep: %d samples\n", idx);
}

/* ---- main ----------------------------------------------------------------------- */

static void scenarios(void)
{
    /* Ride */
    base_model();
    show(UI_TAB_RIDE, false);
    run_scenario("ride_normal");

    s_m.link_ok = false;
    run_scenario("ride_link_lost");

    base_model();
    s_m.speed_mm_s = -1500;
    s_m.measure_mm[0] = -350;
    s_m.measure_mm[1] = 12345;
    run_scenario("ride_backward");

    base_model();
    s_m.speed_mm_s = 5000;
    s_m.measure_mm[0] = 99999990;
    s_m.measure_mm[1] = -99999990;
    s_m.trip_m = 99999;
    s_m.total_m = 9999999;
    run_scenario("ride_max_values");

    base_model();
    s_m.measure_running[0] = false;
    s_m.measure_running[1] = false;
    s_m.measure_decimals = 0;
    run_scenario("ride_stopped");

    /* Lights (ADR 0018) */
    base_model();
    s_m.ride_panel = UI_RIDE_PANEL_LIGHTS;
    run_scenario("ride_lights");
    s_m.lights = UP_SIGNAL_LEFT | UP_LIGHTS_BLINK_ON | UP_LIGHTS_LIGHT_ON;
    run_scenario("ride_lights_signal_left");
    s_m.lights = UP_SIGNAL_HAZARD | UP_LIGHTS_BLINK_ON | UP_LIGHTS_BRAKE_ON;
    run_scenario("ride_lights_hazard_brake");
    s_m.ride_panel = UI_RIDE_PANEL_MEASURE;
    s_m.lights = UP_SIGNAL_RIGHT | UP_LIGHTS_BLINK_ON;
    run_scenario("ride_measure_signal_right");

    /* LED */
    base_model();
    show(UI_TAB_LED, false);
    run_scenario("led_top");
    /* user overlay active: no hint */
    s_m.active_overlay = 8;
    s_m.overlay.level_start = 35;
    run_scenario("led_overlay_user");
    /* built-in overlay, no free user slot (ADR 0017) */
    base_model();
    s_m.led_notice = UI_LED_NOTICE_NO_FREE_SLOT;
    show(UI_TAB_LED, false);
    run_scenario("led_overlay_no_free_slot");
    base_model();
    s_m.editor_on = true;
    apply();
    show(UI_TAB_LED, true);
    run_scenario("led_bottom_editor");

    /* Settings */
    base_model();
    show(UI_TAB_SETTINGS, false);
    run_scenario("settings_top");
    show(UI_TAB_SETTINGS, false);
    ui_scroll_page_y(330);
    run_scenario("settings_strip");
    ui_scroll_page_y(1000);
    run_scenario("settings_light");
    s_m.main_leds = UP_STRIP_MAIN_MAX;
    s_m.front_leds = 0;
    s_m.rear_leds = UP_STRIP_END_MAX;
    s_m.layout_flags = UP_LAYOUT_LEFT_DATA_FRONT;
    s_m.brake_threshold = 30;
    s_m.light_brightness = 100;
    s_m.brake_enabled = false;
    apply();
    ui_scroll_page_y(330);
    run_scenario("settings_strip_max");
    history(UI_CALIB_HISTORY);
    apply();
    show(UI_TAB_SETTINGS, true);
    run_scenario("settings_history_full");
    history(0);
    s_m.time_valid = false;
    apply();
    show(UI_TAB_SETTINGS, true);
    run_scenario("settings_history_empty");

    /* Calibration dialog */
    base_model();
    s_m.calib_state = UI_CALIB_COUNTING;
    s_m.calib_counts = 579489;
    s_m.calib_distance_mm = 15356;
    run_scenario("calib_counting");
    s_m.calib_state = UI_CALIB_STOPPED;
    run_scenario("calib_input_empty");
    s_m.calib_counts = 500;
    run_scenario("calib_too_short");
    s_m.calib_counts = -2000;
    run_scenario("calib_negative");
    s_m.calib_counts = 579489;
    apply();
    ui_calib_test_input("1520", false);
    run_scenario("calib_valid");
    ui_calib_test_input("1000", false);
    run_scenario("calib_warn");
    ui_calib_test_input(NULL, true);
    run_scenario("calib_confirm");
    s_m.calib_state = UI_CALIB_IDLE;
    apply();

    /* Date/time dialog */
    ui_time_dialog_test(true);
    run_scenario("time_dialog");
    ui_time_dialog_test(false);
}

int check_run(bool update_references)
{
    s_update_refs = update_references;
    struct stat st;
    s_refs_enabled = stat(SIM_REFERENCE_DIR, &st) == 0 && S_ISDIR(st.st_mode);
    mkdir(SIM_CHECK_OUT_DIR, 0755);
    if (update_references) {
        mkdir(SIM_REFERENCE_DIR, 0755);
    }

    headless_init();
    static const ui_actions_t no_actions = {0};
    ui_init(&no_actions, ui_font_data, ui_font_data_size);

    printf("GUI checks (ADR 0016) – rules in docs/ui-guidelines.md\n");
    scenarios();
    layout_sweep();

    if (s_refs_skipped > 0) {
        printf("      reference comparison skipped (%s does not exist yet, ADR 0016)\n", SIM_REFERENCE_DIR);
    }
    printf("%s: %d checks, %d failures%s\n", s_failures ? "FAILED" : "PASSED", s_checks, s_failures,
           update_references ? " (references updated)" : "");
    printf("      screenshots: %s\n", SIM_CHECK_OUT_DIR);
    return s_failures ? 1 : 0;
}
