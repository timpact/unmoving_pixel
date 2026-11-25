/* Simulated core for the dash simulator (see sim_core.h). */
#include "sim_core.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "measurement.h"

#define CYCLE_MS      60000u   /* ride profile repeats every 60 s */
#define CRUISE_MM_S   3500.0f  /* 3.5 m/s = 12.6 km/h */
#define SMOOTH_MS     300.0f   /* display smoothing (ADR: 200–500 ms) */

typedef struct {
    uint8_t slot;
    const char *name;
    up_overlay_t ov;
} overlay_preset_t;

static const overlay_preset_t k_overlays[] = {
    {0, "Ohne", {UP_OVERLAY_TYPE_EDGE_FADE, 100, 100, 100, 0, 0}},
    {1, "Weiche Ränder", {UP_OVERLAY_TYPE_EDGE_FADE, 10, 100, 10, 150, 150}},
    {2, "Spotlight", {UP_OVERLAY_TYPE_EDGE_FADE, 0, 100, 0, 250, 250}},
    {3, "Nach vorn heller", {UP_OVERLAY_TYPE_EDGE_FADE, 20, 100, 100, 350, 0}},
    {8, "Mein Overlay", {UP_OVERLAY_TYPE_EDGE_FADE, 30, 90, 60, 100, 200}},
};

static const struct {
    uint8_t slot;
    const char *name;
} k_patterns[] = {
    {0, "Streifen"},
    {1, "Punkte"},
    {2, "Regenbogen"},
    {3, "Zebra"},
    {8, "Lauflicht rot"},
    {9, "10-m-Muster"},
};

static struct {
    sim_options_t opt;
    uint32_t t_ms;
    float speed_mm_s;
    float speed_smooth;
    double total_mm_f;     /* exact position, double to avoid drift */
    uint32_t trip_start_mm;
    measurement_t meas[UI_MEASURE_COUNT];
    uint8_t display_brightness;
    uint8_t measure_decimals;
    uint8_t brightness;
    up_overlay_t overlay;
    uint8_t active_pattern;
    uint8_t active_overlay;
    bool editor_on;
} s;

static float smoothstep(float t)
{
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    return t * t * (3 - 2 * t);
}

static float speed_profile(uint32_t t_ms)
{
    float t = (t_ms % CYCLE_MS) / 1000.0f;
    const float pi = 3.14159265f;
    if (t < 8) {
        return CRUISE_MM_S * smoothstep(t / 8);                              /* accelerate */
    }
    if (t < 40) {
        return CRUISE_MM_S + 400.0f * sinf(2 * pi * (t - 8) / 12);          /* cruise */
    }
    if (t < 48) {
        float v40 = CRUISE_MM_S + 400.0f * sinf(2 * pi * 32 / 12);
        return v40 * (1 - smoothstep((t - 40) / 8));                         /* brake */
    }
    return 0;                                                                /* stand */
}

static uint32_t total_mm(void)
{
    return (uint32_t)fmod(s.total_mm_f, 4294967296.0);
}

void sim_core_init(const sim_options_t *opt)
{
    memset(&s, 0, sizeof(s));
    s.opt = *opt;
    s.total_mm_f = 1234567.0 * 1000.0;            /* 1,234.567 km */
    s.trip_start_mm = total_mm() - 12340u * 1000u; /* trip: 12.34 km */
    s.brightness = 80;
    s.display_brightness = 70;
    s.measure_decimals = 2;
    s.overlay = k_overlays[1].ov;
    s.active_pattern = 0;
    s.active_overlay = 1;
}

void sim_core_step(uint32_t dt_ms)
{
    s.t_ms += dt_ms;
    s.speed_mm_s = speed_profile(s.t_ms);
    s.total_mm_f += s.speed_mm_s * dt_ms / 1000.0;
    float a = dt_ms / (SMOOTH_MS + dt_ms);
    s.speed_smooth += a * (s.speed_mm_s - s.speed_smooth);
}

void sim_core_fill_model(ui_model_t *m)
{
    uint32_t tot = total_mm();

    m->link_ok = !(s.opt.link_drops && (s.t_ms % 30000u) >= 20000u && (s.t_ms % 30000u) < 22000u);
    m->speed_mm_s = (uint16_t)(s.speed_smooth + 0.5f);
    m->total_m = (uint32_t)(s.total_mm_f / 1000.0);
    m->trip_m = (tot - s.trip_start_mm) / 1000u;
    m->battery_mv = 7420;

    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        m->measure_mm[i] = measurement_value(&s.meas[i], tot, NULL);
        m->measure_running[i] = s.meas[i].running;
    }
    m->display_brightness = s.display_brightness;
    m->measure_decimals = s.measure_decimals;

    m->global_brightness = s.brightness;
    m->overlay = s.overlay;
    m->active_pattern = s.active_pattern;
    m->active_overlay = s.active_overlay;
    m->strip_length_mm = 500;

    m->patterns.rev = 1;
    m->patterns.count = sizeof(k_patterns) / sizeof(k_patterns[0]);
    for (int i = 0; i < m->patterns.count; i++) {
        m->patterns.entries[i].slot = k_patterns[i].slot;
        m->patterns.entries[i].builtin = k_patterns[i].slot < 8;
        snprintf(m->patterns.entries[i].name, UI_PRESET_NAME_LEN, "%s", k_patterns[i].name);
    }
    m->overlays.rev = 1;
    m->overlays.count = sizeof(k_overlays) / sizeof(k_overlays[0]);
    for (int i = 0; i < m->overlays.count; i++) {
        m->overlays.entries[i].slot = k_overlays[i].slot;
        m->overlays.entries[i].builtin = k_overlays[i].slot < 8;
        snprintf(m->overlays.entries[i].name, UI_PRESET_NAME_LEN, "%s", k_overlays[i].name);
    }

    m->editor_on = s.editor_on;
    snprintf(m->wifi_ssid, sizeof(m->wifi_ssid), "unmoving-pixel");
    snprintf(m->wifi_password, sizeof(m->wifi_password), "k7Fq-29xa-Lm4d");
    snprintf(m->editor_url, sizeof(m->editor_url), "http://192.168.4.1");
}

void sim_core_set_editor(bool on)
{
    s.editor_on = on;
}

/* ---- GUI actions -------------------------------------------------------- */

static void act_measure_start(void *ctx, uint8_t i)
{
    measurement_start(&s.meas[i], total_mm());
    printf("[action] measurement %u start\n", i + 1);
}

static void act_measure_stop(void *ctx, uint8_t i)
{
    measurement_stop(&s.meas[i], total_mm());
    printf("[action] measurement %u stop\n", i + 1);
}

static void act_measure_reset(void *ctx, uint8_t i)
{
    measurement_reset(&s.meas[i], total_mm());
    printf("[action] measurement %u reset\n", i + 1);
}

static void act_set_display_brightness(void *ctx, uint8_t percent)
{
    s.display_brightness = percent;
    printf("[action] display brightness %u %%\n", percent);
}

static void act_set_measure_decimals(void *ctx, uint8_t decimals)
{
    s.measure_decimals = decimals;
    printf("[action] measurement resolution: %u decimals\n", decimals);
}

static void act_set_brightness(void *ctx, uint8_t percent)
{
    s.brightness = percent;
    printf("[action] brightness %u %%\n", percent);
}

static void act_set_overlay(void *ctx, const up_overlay_t *o)
{
    s.overlay = *o;
    s.active_overlay = UI_OVERLAY_MODIFIED;
    printf("[action] overlay %u/%u/%u %% fades %u/%u mm\n",
           o->level_start, o->level_mid, o->level_end, o->fade_start_mm, o->fade_end_mm);
}

static void act_select_preset(void *ctx, uint8_t category, uint8_t slot)
{
    if (category == UI_PRESET_PATTERN) {
        s.active_pattern = slot;
        printf("[action] pattern slot %u\n", slot);
        return;
    }
    for (size_t i = 0; i < sizeof(k_overlays) / sizeof(k_overlays[0]); i++) {
        if (k_overlays[i].slot == slot) {
            s.overlay = k_overlays[i].ov;
            s.active_overlay = slot;
            printf("[action] overlay preset slot %u (%s)\n", slot, k_overlays[i].name);
        }
    }
}

static void act_set_editor(void *ctx, bool on)
{
    s.editor_on = on;
    printf("[action] editor %s\n", on ? "on" : "off");
}

const ui_actions_t sim_core_actions = {
    .ctx = NULL,
    .measure_start = act_measure_start,
    .measure_stop = act_measure_stop,
    .measure_reset = act_measure_reset,
    .set_display_brightness = act_set_display_brightness,
    .set_measure_decimals = act_set_measure_decimals,
    .set_brightness = act_set_brightness,
    .set_overlay = act_set_overlay,
    .select_preset = act_select_preset,
    .set_editor = act_set_editor,
};
