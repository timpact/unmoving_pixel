/*
 * Unit test for the dash-owned state (ADR 0019 "NVS on the dash"): defaults,
 * loading with range checks, delayed saving of changed values only, RAM-only
 * measurements and ride panel.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dash_state.h"

/* store with three keys in RAM, counts writes */
typedef struct {
    const char *key;
    bool present;
    uint8_t value;
    int writes;
} entry_t;

static entry_t s_entries[3] = {
    {DASH_KEY_DISPLAY_BRIGHTNESS, false, 0, 0},
    {DASH_KEY_MEASURE_DECIMALS, false, 0, 0},
    {DASH_KEY_MEASURE_ADD_BW, false, 0, 0},
};

static entry_t *find(const char *key)
{
    for (int i = 0; i < 3; i++) {
        if (strcmp(s_entries[i].key, key) == 0) return &s_entries[i];
    }
    assert(!"unknown key");
    return NULL;
}

static bool get_u8(void *ctx, const char *key, uint8_t *v)
{
    (void)ctx;
    entry_t *e = find(key);
    if (e->present) *v = e->value;
    return e->present;
}

static void set_u8(void *ctx, const char *key, uint8_t v)
{
    (void)ctx;
    entry_t *e = find(key);
    e->present = true;
    e->value = v;
    e->writes++;
}

static int total_writes(void)
{
    return s_entries[0].writes + s_entries[1].writes + s_entries[2].writes;
}

static void core_set_brightness(void *ctx, uint8_t percent)
{
    (void)ctx;
    (void)percent;
}

static const dash_store_t k_store = {NULL, get_u8, set_u8};

int main(void)
{
    ui_model_t m;
    ui_actions_t core = {0}, a;

    /* empty store: defaults, ride panel starts with "Licht", nothing written */
    dash_state_init(&k_store);
    dash_state_actions(&a, &core);
    dash_state_update(0, 1000, 1000);
    memset(&m, 0, sizeof(m));
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_DEF_DISPLAY_BRIGHTNESS);
    assert(m.measure_decimals == DASH_DEF_MEASURE_DECIMALS);
    assert(!m.measure_add_backward);
    assert(m.ride_panel == UI_RIDE_PANEL_LIGHTS);
    dash_state_update(10000, 1000, 1000);
    assert(total_writes() == 0);

    /* slider: many changes, one write 2 s after the last one */
    for (uint32_t t = 100; t <= 1000; t += 100) {
        dash_state_update(t, 1000, 1000);
        a.set_display_brightness(NULL, (uint8_t)(20 + t / 100));
    }
    dash_state_update(2999, 1000, 1000);
    assert(total_writes() == 0);
    dash_state_update(3000, 1000, 1000);
    assert(s_entries[0].writes == 1 && s_entries[0].value == 30 && total_writes() == 1);

    /* change and change back: nothing to write */
    a.set_measure_decimals(NULL, 0);
    a.set_measure_decimals(NULL, DASH_DEF_MEASURE_DECIMALS);
    dash_state_update(10000, 1000, 1000);
    assert(total_writes() == 1);

    /* out of range is clamped or ignored */
    a.set_display_brightness(NULL, 0);
    a.set_measure_decimals(NULL, 7);
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_MIN_DISPLAY_BRIGHTNESS && m.measure_decimals == DASH_DEF_MEASURE_DECIMALS);

    dash_state_update(15000, 1000, 1000);              /* write the clamped brightness */

    /* measurements and ride panel: RAM only, never written */
    int before = total_writes();
    a.measure_start(NULL, 0);
    dash_state_update(20000, 3000, 3500);              /* net +2500 */
    dash_state_fill_model(&m);
    assert(m.measure_running[0] && m.measure_mm[0] == 2500 && !m.measure_running[1]);
    a.set_measure_add_backward(NULL, true);            /* counter switch: no jump */
    dash_state_fill_model(&m);
    assert(m.measure_mm[0] == 2500 && m.measure_add_backward);
    a.measure_stop(NULL, 0);
    a.set_ride_panel(NULL, UI_RIDE_PANEL_MEASURE);
    dash_state_update(30000, 9000, 9000);
    dash_state_fill_model(&m);
    assert(m.measure_mm[0] == 2500 && !m.measure_running[0] && m.ride_panel == UI_RIDE_PANEL_MEASURE);
    assert(total_writes() == before + 1 && s_entries[2].value == 1);   /* only meas_add_bw */

    /* restart: settings come back, measurements and ride panel do not */
    dash_state_init(&k_store);
    dash_state_update(0, 9000, 9000);
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_MIN_DISPLAY_BRIGHTNESS);
    assert(m.measure_add_backward);
    assert(m.measure_mm[0] == 0 && !m.measure_running[0]);
    assert(m.ride_panel == UI_RIDE_PANEL_LIGHTS);

    /* stored values out of range give the defaults */
    s_entries[0].value = 200;
    s_entries[1].value = 3;
    s_entries[2].value = 2;
    dash_state_init(&k_store);
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_DEF_DISPLAY_BRIGHTNESS);
    assert(m.measure_decimals == DASH_DEF_MEASURE_DECIMALS && !m.measure_add_backward);

    /* other actions pass through to core */
    core.set_brightness = core_set_brightness;
    dash_state_actions(&a, &core);
    assert(a.set_brightness == core.set_brightness);

    printf("test_dash_state: OK\n");
    return 0;
}
