/* Simulated core for the dash simulator (see sim_core.h). */
#include "sim_core.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "measurement.h"
#include "up_lights.h"
#include "up_preset.h"
#include "up_strip.h"

/* calendar helpers from dash/ui (ui.c) */
uint32_t ui_make_time(int year, int month, int day, int hour, int minute);
void ui_fmt_datetime(char *buf, size_t len, uint32_t t);

#define CYCLE_MS      60000u   /* ride profile repeats every 60 s */
#define CRUISE_MM_S   3500.0f  /* 3.5 m/s = 12.6 km/h */
#define SMOOTH_MS     300.0f   /* display smoothing (ADR: 200–500 ms) */
#define TRUE_NM       26180u   /* simulated real factor of the encoder (nm per count) */
#define HYSTERESIS_MM 5.0      /* direction hysteresis for the travelled distance (ADR 0015) */

typedef struct {
    uint8_t slot;
    const char *name;
    up_overlay_t ov;
} overlay_preset_t;

/* Overlay slots as on core (mock numbering, see tools/preset-editor/mock-dash.mjs):
   0 … 7 built-in (read-only), 8 … 15 user */
#define OV_BUILTIN_SLOTS 8
#define OV_USER_SLOTS    8
#define OV_SLOTS         (OV_BUILTIN_SLOTS + OV_USER_SLOTS)
#define NOTICE_MS        5000u

typedef struct {
    bool used;
    char name[UP_PRESET_NAME_BYTES + 1];
    up_overlay_t ov;
} overlay_slot_t;

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
    double pos_mm_f;       /* net position (backward subtracts), double to avoid drift */
    double total_mm_f;     /* travelled distance (backward adds, with hysteresis) */
    double hyst_anchor;    /* position at which travelled distance was last counted */
    int hyst_dir;          /* current direction for the hysteresis: +1 / -1 */
    uint32_t trip_start_mm;
    measurement_t meas[UI_MEASURE_COUNT];
    uint8_t display_brightness;
    uint8_t measure_decimals;
    bool measure_add_backward;
    uint8_t brightness;
    up_overlay_t overlay;
    uint8_t active_pattern;
    uint8_t active_overlay;
    overlay_slot_t ovs[OV_SLOTS];
    uint16_t overlay_list_rev;
    uint8_t led_notice;
    uint32_t notice_until_ms;
    bool editor_on;
    up_lights_t lights;      /* end lights (ADR 0018), same code as on core */
    uint8_t ride_panel;
    uint8_t main_leds;
    uint8_t front_leds;
    uint8_t rear_leds;
    uint8_t layout_flags;
    /* calibration (ADR 0014) */
    uint8_t calib_state;
    double calib_start_mm;
    int32_t calib_counts;
    uint32_t nm_per_count;
    uint8_t hist_count;
    ui_calib_entry_t hist[UI_CALIB_HISTORY];
    /* clock: valid after set_time */
    bool time_valid;
    int64_t time_base;   /* local time at t_ms = 0 */
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
    if (t < 44) {
        float v40 = CRUISE_MM_S + 400.0f * sinf(2 * pi * 32 / 12);
        return v40 * (1 - smoothstep((t - 40) / 4));   /* brake, peak ~1.2 m/s²: brake light (ADR 0018) */
    }
    if (t >= 50 && t < 53) {
        return -350.0f * sinf(pi * (t - 50) / 3);                            /* roll back ~0.67 m */
    }
    return 0;                                                                /* stand */
}

/* uint32 wire value of a (possibly negative) distance, wrapping at 2^32 */
static uint32_t wire(double mm)
{
    return (uint32_t)((int64_t)floor(mm) & 0xFFFFFFFF);
}

static uint32_t total_mm(void)
{
    return wire(s.total_mm_f);
}

static uint32_t position_mm(void)
{
    return wire(s.pos_mm_f);
}

/* counter used by the measurements (ADR 0015) */
static uint32_t measure_counter(void)
{
    return s.measure_add_backward ? total_mm() : position_mm();
}

/* travelled distance with direction hysteresis (ADR 0015) */
static void update_travelled(void)
{
    double d = s.pos_mm_f - s.hyst_anchor;
    if (d * s.hyst_dir > 0) {               /* moving on in the current direction */
        s.total_mm_f += fabs(d);
        s.hyst_anchor = s.pos_mm_f;
    } else if (fabs(d) >= HYSTERESIS_MM) {  /* reversal confirmed */
        s.hyst_dir = -s.hyst_dir;
        s.total_mm_f += fabs(d);
        s.hyst_anchor = s.pos_mm_f;
    }                                       /* else: jitter within the hysteresis */
}

void sim_core_init(const sim_options_t *opt)
{
    memset(&s, 0, sizeof(s));
    s.opt = *opt;
    s.total_mm_f = 1234567.0 * 1000.0;            /* 1,234.567 km */
    s.hyst_dir = 1;
    s.trip_start_mm = total_mm() - 12340u * 1000u; /* trip: 12.34 km */
    s.brightness = 80;
    up_lights_init(&s.lights, 0);
    s.main_leds = UP_STRIP_MAIN_DEF;
    s.front_leds = UP_STRIP_END_DEF;
    s.rear_leds = UP_STRIP_END_DEF;
    s.layout_flags = UP_LAYOUT_LEFT_DATA_FRONT | UP_LAYOUT_RIGHT_DATA_FRONT;
    s.display_brightness = 70;
    s.measure_decimals = 2;
    for (size_t i = 0; i < sizeof(k_overlays) / sizeof(k_overlays[0]); i++) {
        overlay_slot_t *o = &s.ovs[k_overlays[i].slot];
        o->used = true;
        snprintf(o->name, sizeof(o->name), "%s", k_overlays[i].name);
        o->ov = k_overlays[i].ov;
    }
    if (opt->overlays_full) {   /* all user slots taken: test the "no free slot" notice */
        for (int slot = OV_BUILTIN_SLOTS; slot < OV_SLOTS; slot++) {
            if (!s.ovs[slot].used) {
                s.ovs[slot].used = true;
                snprintf(s.ovs[slot].name, sizeof(s.ovs[slot].name), "Eigenes %d", slot - OV_BUILTIN_SLOTS + 1);
                s.ovs[slot].ov = k_overlays[0].ov;
            }
        }
    }
    s.overlay_list_rev = 1;
    s.active_pattern = 0;
    s.active_overlay = 1;
    s.overlay = s.ovs[1].ov;

    /* calibration: stored factor slightly off, two older entries */
    s.nm_per_count = 26500;
    s.hist_count = 2;
    s.hist[0] = (ui_calib_entry_t){ui_make_time(2026, 10, 3, 14, 20), 1180000, 1000, 37736, 26500};
    s.hist[1] = (ui_calib_entry_t){0, 950000, 2000, 75988, 26320};
}

void sim_core_step(uint32_t dt_ms)
{
    s.t_ms += dt_ms;
    if (s.led_notice != UI_LED_NOTICE_NONE && (int32_t)(s.t_ms - s.notice_until_ms) >= 0) {
        s.led_notice = UI_LED_NOTICE_NONE;
    }
    s.speed_mm_s = speed_profile(s.t_ms);
    s.pos_mm_f += s.speed_mm_s * dt_ms / 1000.0;
    update_travelled();
    up_lights_update(&s.lights, s.t_ms, (int32_t)lroundf(s.speed_mm_s), total_mm());
    float a = dt_ms / (SMOOTH_MS + dt_ms);
    s.speed_smooth += a * (s.speed_mm_s - s.speed_smooth);

    if (s.calib_state == UI_CALIB_COUNTING) {
        double net = s.pos_mm_f - s.calib_start_mm;   /* simulated true net distance */
        s.calib_counts = (int32_t)(net * 1e6 / TRUE_NM);
    }
}

void sim_core_fill_model(ui_model_t *m)
{
    uint32_t tot = total_mm();

    m->link_ok = !(s.opt.link_drops && (s.t_ms % 30000u) >= 20000u && (s.t_ms % 30000u) < 22000u);
    m->speed_mm_s = (int16_t)lroundf(s.speed_smooth);
    m->total_m = (uint32_t)(s.total_mm_f / 1000.0);
    m->trip_m = (tot - s.trip_start_mm) / 1000u;
    m->battery_mv = 7420;

    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        m->measure_mm[i] = measurement_value(&s.meas[i], measure_counter(), NULL);
        m->measure_running[i] = s.meas[i].running;
    }
    m->display_brightness = s.display_brightness;
    m->measure_decimals = s.measure_decimals;
    m->measure_add_backward = s.measure_add_backward;

    m->global_brightness = s.brightness;
    m->overlay = s.overlay;
    m->active_pattern = s.active_pattern;
    m->active_overlay = s.active_overlay;
    m->led_notice = s.led_notice;
    m->strip_length_mm = UP_STRIP_MAIN_MM(s.main_leds);
    m->main_leds = s.main_leds;
    m->front_leds = s.front_leds;
    m->rear_leds = s.rear_leds;
    m->layout_flags = s.layout_flags;
    m->lights = up_lights_status(&s.lights);
    m->light_brightness = s.lights.light_brightness;
    m->brake_enabled = s.lights.brake_enabled;
    m->brake_threshold = s.lights.brake_threshold;
    m->ride_panel = s.ride_panel;

    m->patterns.rev = 1;
    m->patterns.count = sizeof(k_patterns) / sizeof(k_patterns[0]);
    for (int i = 0; i < m->patterns.count; i++) {
        m->patterns.entries[i].slot = k_patterns[i].slot;
        m->patterns.entries[i].builtin = k_patterns[i].slot < 8;
        snprintf(m->patterns.entries[i].name, UI_PRESET_NAME_LEN, "%s", k_patterns[i].name);
    }
    m->overlays.rev = s.overlay_list_rev;
    m->overlays.count = 0;
    for (int slot = 0; slot < OV_SLOTS; slot++) {
        if (!s.ovs[slot].used) {
            continue;
        }
        ui_preset_entry_t *e = &m->overlays.entries[m->overlays.count++];
        e->slot = (uint8_t)slot;
        e->builtin = slot < OV_BUILTIN_SLOTS;
        snprintf(e->name, UI_PRESET_NAME_LEN, "%s", s.ovs[slot].name);
    }

    m->calib_state = s.calib_state;
    m->calib_counts = s.calib_counts;
    m->calib_distance_mm = (int32_t)((int64_t)s.calib_counts * s.nm_per_count / 1000000);
    m->nm_per_count = s.nm_per_count;
    m->calib_history_count = s.hist_count;
    memcpy(m->calib_history, s.hist, sizeof(s.hist));

    m->time_valid = s.time_valid;
    m->time_now = s.time_valid ? (uint32_t)(s.time_base + s.t_ms / 1000u) : 0;

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
    measurement_start(&s.meas[i], measure_counter());
    printf("[action] measurement %u start\n", i + 1);
}

static void act_measure_stop(void *ctx, uint8_t i)
{
    measurement_stop(&s.meas[i], measure_counter());
    printf("[action] measurement %u stop\n", i + 1);
}

static void act_measure_reset(void *ctx, uint8_t i)
{
    measurement_reset(&s.meas[i], measure_counter());
    printf("[action] measurement %u reset\n", i + 1);
}

static void act_set_measure_add_backward(void *ctx, bool add)
{
    if (add == s.measure_add_backward) {
        return;
    }
    uint32_t old_counter = measure_counter();
    s.measure_add_backward = add;
    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        measurement_switch_counter(&s.meas[i], old_counter, measure_counter());
    }
    printf("[action] measurements: backward travel %s\n", add ? "added" : "subtracted");
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

/* SET_OVERLAY (ADR 0004, 0017): core writes into the active preset; a built-in
   preset is copied to a free user slot first */
static void act_set_overlay(void *ctx, const up_overlay_t *o)
{
    uint8_t slot = s.active_overlay;
    if (slot < OV_BUILTIN_SLOTS) {
        int free_slot = -1;
        const char *names[OV_SLOTS];
        size_t n = 0;
        for (int i = 0; i < OV_SLOTS; i++) {
            if (s.ovs[i].used) {
                names[n++] = s.ovs[i].name;
            } else if (i >= OV_BUILTIN_SLOTS && free_slot < 0) {
                free_slot = i;
            }
        }
        if (free_slot < 0) {
            /* ACK with "no free slot": values stay those of the built-in preset */
            s.led_notice = UI_LED_NOTICE_NO_FREE_SLOT;
            s.notice_until_ms = s.t_ms + NOTICE_MS;
            printf("[action] overlay: no free user slot\n");
            return;
        }
        overlay_slot_t *copy = &s.ovs[free_slot];
        up_preset_copy_name(s.ovs[slot].name, names, n, copy->name);
        copy->used = true;
        slot = (uint8_t)free_slot;
        s.active_overlay = slot;
        s.overlay_list_rev++;
        printf("[action] overlay: built-in copied to slot %u (%s)\n", slot, copy->name);
    }
    s.ovs[slot].ov = *o;
    s.overlay = *o;
    s.led_notice = UI_LED_NOTICE_NONE;
    printf("[action] overlay slot %u: %u/%u/%u %% fades %u/%u mm\n", slot,
           o->level_start, o->level_mid, o->level_end, o->fade_start_mm, o->fade_end_mm);
}

static void act_select_preset(void *ctx, uint8_t category, uint8_t slot)
{
    if (category == UI_PRESET_PATTERN) {
        s.active_pattern = slot;
        printf("[action] pattern slot %u\n", slot);
        return;
    }
    if (slot < OV_SLOTS && s.ovs[slot].used) {
        s.overlay = s.ovs[slot].ov;
        s.active_overlay = slot;
        s.led_notice = UI_LED_NOTICE_NONE;
        printf("[action] overlay preset slot %u (%s)\n", slot, s.ovs[slot].name);
    }
}

static void act_set_editor(void *ctx, bool on)
{
    s.editor_on = on;
    printf("[action] editor %s\n", on ? "on" : "off");
}

static void act_calib_start(void *ctx)
{
    s.calib_state = UI_CALIB_COUNTING;
    s.calib_start_mm = s.pos_mm_f;
    s.calib_counts = 0;
    printf("[action] calibration start\n");
}

static void act_calib_stop(void *ctx)
{
    s.calib_state = UI_CALIB_STOPPED;
    printf("[action] calibration stop: %" PRId32 " net counts\n", s.calib_counts);
}

static void act_calib_apply(void *ctx, uint32_t ref_cm)
{
    if (s.calib_state != UI_CALIB_STOPPED || s.calib_counts <= 0) {
        return;
    }
    uint32_t nm = (uint32_t)((uint64_t)ref_cm * 10000000u / (uint32_t)s.calib_counts);
    memmove(&s.hist[1], &s.hist[0], sizeof(s.hist[0]) * (UI_CALIB_HISTORY - 1));
    s.hist[0] = (ui_calib_entry_t){
        s.time_valid ? (uint32_t)(s.time_base + s.t_ms / 1000u) : 0,
        (uint32_t)(s.total_mm_f / 1000.0), ref_cm, (uint32_t)s.calib_counts, nm};
    if (s.hist_count < UI_CALIB_HISTORY) {
        s.hist_count++;
    }
    s.nm_per_count = nm;
    s.calib_state = UI_CALIB_IDLE;
    printf("[action] calibration apply: %" PRIu32 " cm / %" PRId32 " counts -> %" PRIu32 " nm per count\n", ref_cm,
           s.calib_counts, nm);
}

static void act_calib_discard(void *ctx)
{
    s.calib_state = UI_CALIB_IDLE;
    printf("[action] calibration discarded\n");
}

static void act_set_time(void *ctx, uint32_t t)
{
    s.time_valid = true;
    s.time_base = (int64_t)t - s.t_ms / 1000u;
    char buf[24];
    ui_fmt_datetime(buf, sizeof(buf), t);
    printf("[action] time set: %s\n", buf);
}

/* ---- ADR 0018 ---------------------------------------------------------------- */

static void act_set_ride_panel(void *ctx, uint8_t panel)
{
    s.ride_panel = panel;
    printf("[action] ride panel %s\n", panel == UI_RIDE_PANEL_LIGHTS ? "lights" : "measurement");
}

static void act_set_signal(void *ctx, uint8_t signal)
{
    static const char *const names[] = {"off", "left", "right", "hazard"};
    up_lights_set_signal(&s.lights, signal, s.t_ms, total_mm());
    printf("[action] signal %s\n", signal <= UP_SIGNAL_HAZARD ? names[signal] : "?");
}

static void act_set_light(void *ctx, bool on)
{
    up_lights_set_light(&s.lights, on, s.t_ms);
    printf("[action] light %s\n", on ? "on" : "off");
}

static void act_set_strip_layout(void *ctx, uint8_t main_leds, uint8_t front, uint8_t rear, uint8_t flags)
{
    s.main_leds = main_leds;
    s.front_leds = front;
    s.rear_leds = rear;
    s.layout_flags = flags;
    printf("[action] strip layout %u + %u + %u LEDs, flags 0x%02x\n", front, main_leds, rear, flags);
}

static void act_set_light_settings(void *ctx, uint8_t brightness, bool brake_enabled, uint8_t threshold)
{
    up_lights_set_settings(&s.lights, brightness, brake_enabled, threshold);
    printf("[action] light %u %%, brake light %s, threshold %u.%u m/s²\n", brightness,
           brake_enabled ? "on" : "off", threshold / 10, threshold % 10);
}

const ui_actions_t sim_core_actions = {
    .ctx = NULL,
    .measure_start = act_measure_start,
    .measure_stop = act_measure_stop,
    .measure_reset = act_measure_reset,
    .set_display_brightness = act_set_display_brightness,
    .set_measure_decimals = act_set_measure_decimals,
    .set_measure_add_backward = act_set_measure_add_backward,
    .set_brightness = act_set_brightness,
    .set_overlay = act_set_overlay,
    .select_preset = act_select_preset,
    .set_editor = act_set_editor,
    .calib_start = act_calib_start,
    .calib_stop = act_calib_stop,
    .calib_apply = act_calib_apply,
    .calib_discard = act_calib_discard,
    .set_time = act_set_time,
    .set_ride_panel = act_set_ride_panel,
    .set_signal = act_set_signal,
    .set_light = act_set_light,
    .set_strip_layout = act_set_strip_layout,
    .set_light_settings = act_set_light_settings,
};
