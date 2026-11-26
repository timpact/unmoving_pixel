/* State owned by the dash, see dash_state.h. */
#include "dash_state.h"

#include <stdio.h>
#include <string.h>

#include "measurement.h"

typedef struct {
    uint8_t display_brightness;
    uint8_t measure_decimals;
    uint8_t measure_add_backward;
} settings_t;

static struct {
    const dash_store_t *store;
    settings_t cur;
    settings_t saved;
    bool dirty;
    uint32_t changed_ms;
    uint32_t now_ms;
    uint32_t total_mm;
    uint32_t position_mm;
    measurement_t meas[UI_MEASURE_COUNT];
    uint8_t ride_panel;
} s;

static uint8_t load(const char *key, uint8_t min, uint8_t max, uint8_t def)
{
    uint8_t v;
    if (s.store == NULL || !s.store->get_u8(s.store->ctx, key, &v) || v < min || v > max) {
        return def;
    }
    return v;
}

void dash_state_init(const dash_store_t *store)
{
    memset(&s, 0, sizeof(s));
    s.store = store;
    s.cur.display_brightness =
        load(DASH_KEY_DISPLAY_BRIGHTNESS, DASH_MIN_DISPLAY_BRIGHTNESS, 100, DASH_DEF_DISPLAY_BRIGHTNESS);
    s.cur.measure_decimals = load(DASH_KEY_MEASURE_DECIMALS, 0, DASH_MAX_MEASURE_DECIMALS, DASH_DEF_MEASURE_DECIMALS);
    s.cur.measure_add_backward = load(DASH_KEY_MEASURE_ADD_BW, 0, 1, 0);
    s.saved = s.cur;
    s.ride_panel = UI_RIDE_PANEL_LIGHTS;   /* not stored, starts with "Licht" (ADR 0018) */
}

static void save_if_changed(const char *key, uint8_t cur, uint8_t *saved)
{
    if (cur != *saved) {
        s.store->set_u8(s.store->ctx, key, cur);
        *saved = cur;
    }
}

void dash_state_flush(void)
{
    if (!s.dirty) {
        return;
    }
    s.dirty = false;
    if (s.store == NULL) {
        s.saved = s.cur;
        return;
    }
    save_if_changed(DASH_KEY_DISPLAY_BRIGHTNESS, s.cur.display_brightness, &s.saved.display_brightness);
    save_if_changed(DASH_KEY_MEASURE_DECIMALS, s.cur.measure_decimals, &s.saved.measure_decimals);
    save_if_changed(DASH_KEY_MEASURE_ADD_BW, s.cur.measure_add_backward, &s.saved.measure_add_backward);
}

void dash_state_update(uint32_t now_ms, uint32_t total_mm, uint32_t position_mm)
{
    s.now_ms = now_ms;
    s.total_mm = total_mm;
    s.position_mm = position_mm;
    if (s.dirty && now_ms - s.changed_ms >= DASH_SAVE_DELAY_MS) {
        dash_state_flush();
    }
}

static void changed(void)
{
    s.dirty = true;
    s.changed_ms = s.now_ms;   /* every change restarts the delay */
}

/* counter used by the measurements (ADR 0015) */
static uint32_t counter(void)
{
    return s.cur.measure_add_backward ? s.total_mm : s.position_mm;
}

void dash_state_fill_model(ui_model_t *m)
{
    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        m->measure_mm[i] = measurement_value(&s.meas[i], counter(), NULL);
        m->measure_running[i] = s.meas[i].running;
    }
    m->display_brightness = s.cur.display_brightness;
    m->measure_decimals = s.cur.measure_decimals;
    m->measure_add_backward = s.cur.measure_add_backward != 0;
    m->ride_panel = s.ride_panel;
}

/* ---- GUI actions -------------------------------------------------------- */

static void act_measure_start(void *ctx, uint8_t i)
{
    (void)ctx;
    if (i < UI_MEASURE_COUNT) {
        measurement_start(&s.meas[i], counter());
        printf("[dash] measurement %u start\n", i + 1);
    }
}

static void act_measure_stop(void *ctx, uint8_t i)
{
    (void)ctx;
    if (i < UI_MEASURE_COUNT) {
        measurement_stop(&s.meas[i], counter());
        printf("[dash] measurement %u stop\n", i + 1);
    }
}

static void act_measure_reset(void *ctx, uint8_t i)
{
    (void)ctx;
    if (i < UI_MEASURE_COUNT) {
        measurement_reset(&s.meas[i], counter());
        printf("[dash] measurement %u reset\n", i + 1);
    }
}

static void act_set_measure_add_backward(void *ctx, bool add)
{
    (void)ctx;
    if ((uint8_t)add == s.cur.measure_add_backward) {
        return;
    }
    uint32_t old_counter = counter();
    s.cur.measure_add_backward = add;
    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        measurement_switch_counter(&s.meas[i], old_counter, counter());
    }
    changed();
    printf("[dash] measurements: backward travel %s\n", add ? "added" : "subtracted");
}

static void act_set_display_brightness(void *ctx, uint8_t percent)
{
    (void)ctx;
    if (percent < DASH_MIN_DISPLAY_BRIGHTNESS) percent = DASH_MIN_DISPLAY_BRIGHTNESS;
    if (percent > 100) percent = 100;
    if (percent != s.cur.display_brightness) {
        s.cur.display_brightness = percent;
        changed();
    }
}

static void act_set_measure_decimals(void *ctx, uint8_t decimals)
{
    (void)ctx;
    if (decimals <= DASH_MAX_MEASURE_DECIMALS && decimals != s.cur.measure_decimals) {
        s.cur.measure_decimals = decimals;
        changed();
        printf("[dash] measurement resolution: %u decimals\n", decimals);
    }
}

static void act_set_ride_panel(void *ctx, uint8_t panel)
{
    (void)ctx;
    s.ride_panel = panel == UI_RIDE_PANEL_MEASURE ? UI_RIDE_PANEL_MEASURE : UI_RIDE_PANEL_LIGHTS;
}

void dash_state_actions(ui_actions_t *out, const ui_actions_t *core)
{
    *out = *core;
    out->measure_start = act_measure_start;
    out->measure_stop = act_measure_stop;
    out->measure_reset = act_measure_reset;
    out->set_display_brightness = act_set_display_brightness;
    out->set_measure_decimals = act_set_measure_decimals;
    out->set_measure_add_backward = act_set_measure_add_backward;
    out->set_ride_panel = act_set_ride_panel;
}
