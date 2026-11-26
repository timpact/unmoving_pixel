/* Simulated core for the dash simulator (see sim_core.h). */
#include "sim_core.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "up_lights.h"
#include "preset_ram.h"
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

#define NOTICE_MS 5000u

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
    uint8_t brightness;
    uint8_t led_notice;
    uint32_t notice_until_ms;
    up_lights_t lights;      /* end lights (ADR 0018), same code as on core */
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
    /* presets: built-ins from presets/ (generated), two demo user presets */
    preset_ram_init();
    static ps_preset_t p;
    uint16_t rev;
    memset(&p, 0, sizeof(p));
    p.category = UP_CAT_OVERLAY;
    snprintf(p.name, sizeof(p.name), "Mein Overlay");
    p.overlay = (up_overlay_t){UP_OVERLAY_TYPE_EDGE_FADE, 30, 90, 60, 100, 200};
    preset_ram_store.put(NULL, UP_CAT_OVERLAY, PRESET_RAM_OVERLAY_BUILTIN, PS_REV_NEW, &p, &rev);
    memset(&p, 0, sizeof(p));
    p.category = UP_CAT_PATTERN;
    snprintf(p.name, sizeof(p.name), "Lauflicht rot");
    p.segment_count = 2;
    p.segments[0] = (up_segment_t){20, 100, 255, 0, 0};
    p.segments[1] = (up_segment_t){80, 0, 0, 0, 0};
    preset_ram_store.put(NULL, UP_CAT_PATTERN, PRESET_RAM_PATTERN_BUILTIN, PS_REV_NEW, &p, &rev);
    if (opt->overlays_full) {   /* all user slots taken: test the "no free slot" notice */
        memset(&p, 0, sizeof(p));
        p.category = UP_CAT_OVERLAY;
        p.overlay = (up_overlay_t){UP_OVERLAY_TYPE_EDGE_FADE, 100, 100, 100, 0, 0};
        for (int i = 1; i < PRESET_RAM_OVERLAY_USER; i++) {
            snprintf(p.name, sizeof(p.name), "Eigenes %d", i + 1);
            preset_ram_store.put(NULL, UP_CAT_OVERLAY, (uint8_t)(PRESET_RAM_OVERLAY_BUILTIN + i), PS_REV_NEW, &p, &rev);
        }
    }
    preset_ram_select(UP_CAT_OVERLAY, 1);

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

/* preset list of the GUI: existing presets only (ADR 0004 PRESET_LIST) */
static void fill_list(uint8_t cat, ui_preset_list_t *l)
{
    static ps_entry_t entries[PS_MAX_SLOTS];
    int n = preset_ram_store.list(NULL, cat, entries, PS_MAX_SLOTS);
    l->rev = preset_ram_store.list_rev(NULL);
    l->count = 0;
    for (int i = 0; i < n && l->count < UI_MAX_PRESETS; i++) {
        if (entries[i].kind == PS_EMPTY) {
            continue;
        }
        ui_preset_entry_t *e = &l->entries[l->count++];
        e->slot = entries[i].slot;
        e->builtin = entries[i].kind == PS_BUILTIN;
        snprintf(e->name, UI_PRESET_NAME_LEN, "%s", entries[i].name);
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

    m->global_brightness = s.brightness;
    m->overlay = preset_ram_overlay();
    m->active_pattern = preset_ram_active(UP_CAT_PATTERN);
    m->active_overlay = preset_ram_active(UP_CAT_OVERLAY);
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

    fill_list(UP_CAT_PATTERN, &m->patterns);
    fill_list(UP_CAT_OVERLAY, &m->overlays);

    m->calib_state = s.calib_state;
    m->calib_counts = s.calib_counts;
    m->calib_distance_mm = (int32_t)((int64_t)s.calib_counts * s.nm_per_count / 1000000);
    m->nm_per_count = s.nm_per_count;
    m->calib_history_count = s.hist_count;
    memcpy(m->calib_history, s.hist, sizeof(s.hist));

    m->time_valid = s.time_valid;
    m->time_now = s.time_valid ? (uint32_t)(s.time_base + s.t_ms / 1000u) : 0;

}

void sim_core_counters(uint32_t *total, uint32_t *position)
{
    *total = total_mm();
    *position = position_mm();
}

/* ---- GUI actions -------------------------------------------------------- */

static void act_set_brightness(void *ctx, uint8_t percent)
{
    s.brightness = percent;
    printf("[action] brightness %u %%\n", percent);
}

/* SET_OVERLAY (ADR 0004, 0017): core writes into the active preset; a built-in
   preset is copied to a free user slot first */
static void act_set_overlay(void *ctx, const up_overlay_t *o)
{
    uint8_t slot;
    ps_result_t r = preset_ram_dash_set_overlay(o, &slot);
    if (r == PS_FULL) {
        /* ACK with "no free slot": values stay those of the built-in preset */
        s.led_notice = UI_LED_NOTICE_NO_FREE_SLOT;
        s.notice_until_ms = s.t_ms + NOTICE_MS;
        printf("[action] overlay: no free user slot\n");
        return;
    }
    s.led_notice = UI_LED_NOTICE_NONE;
    printf("[action] overlay slot %u: %u/%u/%u %% fades %u/%u mm\n", slot,
           o->level_start, o->level_mid, o->level_end, o->fade_start_mm, o->fade_end_mm);
}

static void act_select_preset(void *ctx, uint8_t category, uint8_t slot)
{
    if (preset_ram_select(category == UI_PRESET_PATTERN ? UP_CAT_PATTERN : UP_CAT_OVERLAY, slot) == PS_OK) {
        if (category != UI_PRESET_PATTERN) {
            s.led_notice = UI_LED_NOTICE_NONE;
        }
        printf("[action] %s slot %u\n", category == UI_PRESET_PATTERN ? "pattern" : "overlay", slot);
    }
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
    .set_brightness = act_set_brightness,
    .set_overlay = act_set_overlay,
    .select_preset = act_select_preset,
    .calib_start = act_calib_start,
    .calib_stop = act_calib_stop,
    .calib_apply = act_calib_apply,
    .calib_discard = act_calib_discard,
    .set_time = act_set_time,
    .set_signal = act_set_signal,
    .set_light = act_set_light,
    .set_strip_layout = act_set_strip_layout,
    .set_light_settings = act_set_light_settings,
};
