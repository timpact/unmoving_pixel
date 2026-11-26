/* End lights of the two LED strips (ADR 0018), see up_lights.h. */
#include "up_lights.h"

#include "up_overlay.h"   /* up_smoothstep() */

#include <math.h>
#include <string.h>

#define SPEED_TAU_MS 100.0f   /* speed smoothing for the brake detection */
#define DECEL_TAU_MS 50.0f    /* smoothing of the deceleration */
#define DETECT_STEP_MS 5u     /* brake detection runs at most every 5 ms */
#define BLINK_IN_MS  100      /* an end starts blinking: base light out, amber in */
#define BLINK_OUT_MS 150

/* LED colours (content, sRGB) as linear RGB */
static const float k_white[3] = {1.0f, 1.0f, 1.0f};
static const float k_red[3] = {1.0f, 0.0f, 0.0f};
static const float k_amber[3] = {1.0f, 0.1878f, 0.0f};   /* sRGB 255, 120, 0 */

float up_srgb_to_linear(float c)
{
    if (c <= 0.0f) {
        return 0.0f;
    }
    if (c >= 1.0f) {
        return 1.0f;
    }
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

/* Smooth flash: rise, full, fall, dark */
float up_blink_envelope(uint32_t ms)
{
    uint32_t p = ms % UP_BLINK_PERIOD_MS;
    if (p < UP_BLINK_RISE_MS) {
        return up_smoothstep((float)p / UP_BLINK_RISE_MS);
    }
    p -= UP_BLINK_RISE_MS;
    if (p < UP_BLINK_FULL_MS) {
        return 1.0f;
    }
    p -= UP_BLINK_FULL_MS;
    if (p < UP_BLINK_FALL_MS) {
        return 1.0f - up_smoothstep((float)p / UP_BLINK_FALL_MS);
    }
    return 0.0f;
}

float up_fade_value(const up_fade_t *f, uint32_t now_ms)
{
    int32_t dt = (int32_t)(now_ms - f->t0_ms);
    if (f->dur_ms == 0 || dt >= (int32_t)f->dur_ms) {
        return f->to;
    }
    if (dt <= 0) {
        return f->from;
    }
    return f->from + (f->to - f->from) * up_smoothstep((float)dt / f->dur_ms);
}

static void fade_to(up_fade_t *f, float target, uint32_t now_ms, uint16_t dur_ms)
{
    if (f->to == target) {
        return;
    }
    f->from = up_fade_value(f, now_ms);
    f->to = target;
    f->t0_ms = now_ms;
    f->dur_ms = dur_ms;
}

static bool side_active(uint8_t signal, int side)
{
    return signal == UP_SIGNAL_HAZARD || signal == (side == UP_SIDE_LEFT ? UP_SIGNAL_LEFT : UP_SIGNAL_RIGHT);
}

/* Amber level of one side (0 … 1, before the light brightness) */
static float amber_level(const up_lights_t *l, int side)
{
    float env = l->blinking[side] ? up_blink_envelope(l->now_ms - l->phase_t0_ms) : l->blink_level_off[side];
    return env * up_fade_value(&l->blink_fade[side], l->now_ms);
}

void up_lights_init(up_lights_t *l, uint32_t now_ms)
{
    memset(l, 0, sizeof(*l));
    l->light_brightness = 60;
    l->brake_enabled = true;
    l->brake_threshold = 10;   /* 1.0 m/s² */
    l->now_ms = now_ms;
    l->last_ms = now_ms;
}

void up_lights_set_settings(up_lights_t *l, uint8_t brightness, bool brake_enabled, uint8_t threshold)
{
    l->light_brightness = brightness < 10 ? 10 : brightness > 100 ? 100 : brightness;
    l->brake_enabled = brake_enabled;
    l->brake_threshold = threshold < 5 ? 5 : threshold > 30 ? 30 : threshold;
}

void up_lights_set_light(up_lights_t *l, bool on, uint32_t now_ms)
{
    l->light_on = on;
    fade_to(&l->light_fade, on ? 1.0f : 0.0f, now_ms, UP_LIGHT_FADE_MS);
}

void up_lights_set_signal(up_lights_t *l, uint8_t signal, uint32_t now_ms, uint32_t total_mm)
{
    if (signal > UP_SIGNAL_HAZARD || signal == l->signal) {
        return;
    }
    l->now_ms = now_ms;
    bool any_before = l->blinking[0] || l->blinking[1];
    if (signal != UP_SIGNAL_OFF) {
        l->signal_t0_ms = now_ms;   /* automatic switch-off counts from the last change */
        l->signal_t0_mm = total_mm;
        if (!any_before) {
            l->phase_t0_ms = now_ms;   /* keep the phase when switching sides */
        }
    }
    for (int side = 0; side < 2; side++) {
        bool active = side_active(signal, side);
        if (active && !l->blinking[side]) {
            l->blinking[side] = true;
            fade_to(&l->blink_fade[side], 1.0f, now_ms, BLINK_IN_MS);
        } else if (!active && l->blinking[side]) {
            l->blink_level_off[side] = up_blink_envelope(now_ms - l->phase_t0_ms);
            l->blinking[side] = false;
            fade_to(&l->blink_fade[side], 0.0f, now_ms, BLINK_OUT_MS);
        }
    }
    l->signal = signal;
}

static void brake_update(up_lights_t *l, uint32_t now_ms, int32_t speed_mm_s)
{
    float v = (float)(speed_mm_s < 0 ? -speed_mm_s : speed_mm_s);
    if (!l->have_speed) {
        l->have_speed = true;
        l->speed_smooth = v;
        l->last_ms = now_ms;
        return;
    }
    uint32_t dt = now_ms - l->last_ms;
    if (dt < DETECT_STEP_MS) {
        return;
    }
    l->last_ms = now_ms;
    float prev = l->speed_smooth;
    l->speed_smooth += (float)dt / (SPEED_TAU_MS + dt) * (v - l->speed_smooth);
    float raw = (prev - l->speed_smooth) * 1000.0f / (float)dt;   /* mm/s², decrease of |v| */
    l->decel += (float)dt / (DECEL_TAU_MS + dt) * (raw - l->decel);

    float thr = l->brake_threshold * 100.0f;   /* 0.1 m/s² → mm/s² */
    bool above = l->decel >= thr && l->speed_smooth > UP_BRAKE_MIN_SPEED;
    if (above && !l->above) {
        l->above_since_ms = now_ms;
    }
    l->above = above;

    if (!l->brake_enabled) {
        if (l->brake) {
            l->brake = false;
            fade_to(&l->brake_fade, 0.0f, now_ms, UP_BRAKE_FALL_MS);
        }
        return;
    }
    if (!l->brake && above && now_ms - l->above_since_ms >= UP_BRAKE_CONFIRM_MS) {
        l->brake = true;
        l->brake_on_ms = now_ms;
        fade_to(&l->brake_fade, 1.0f, now_ms, UP_BRAKE_RISE_MS);
    } else if (l->brake && now_ms - l->brake_on_ms >= UP_BRAKE_MIN_ON_MS && l->decel < thr / 2.0f) {
        l->brake = false;
        fade_to(&l->brake_fade, 0.0f, now_ms, UP_BRAKE_FALL_MS);
    }
}

void up_lights_update(up_lights_t *l, uint32_t now_ms, int32_t speed_mm_s, uint32_t total_mm)
{
    l->now_ms = now_ms;
    if ((l->signal == UP_SIGNAL_LEFT || l->signal == UP_SIGNAL_RIGHT) &&
        (now_ms - l->signal_t0_ms >= UP_SIGNAL_TIMEOUT_MS || total_mm - l->signal_t0_mm >= UP_SIGNAL_TIMEOUT_MM)) {
        up_lights_set_signal(l, UP_SIGNAL_OFF, now_ms, total_mm);
    }
    brake_update(l, now_ms, speed_mm_s);
}

static void put(float dst[3], const float color[3], float perceptual, float base_factor, const float base[3])
{
    float lin = up_srgb_to_linear(perceptual);
    for (int k = 0; k < 3; k++) {
        float v = color[k] * lin + base[k] * base_factor;
        dst[k] = v > 1.0f ? 1.0f : v;
    }
}

void up_lights_output(const up_lights_t *l, up_lights_out_t *out)
{
    float b = l->light_brightness / 100.0f;
    float light = up_fade_value(&l->light_fade, l->now_ms);
    float brake = up_fade_value(&l->brake_fade, l->now_ms);
    float rear_level = light * UP_TAIL_LEVEL;
    if (brake > rear_level) {
        rear_level = brake;
    }
    float front_base[3], rear_base[3];
    float lf = up_srgb_to_linear(light * b);
    float lr = up_srgb_to_linear(rear_level * b);
    for (int k = 0; k < 3; k++) {
        front_base[k] = k_white[k] * lf;
        rear_base[k] = k_red[k] * lr;
    }
    for (int side = 0; side < 2; side++) {
        float amber = amber_level(l, side) * b;
        float keep = 1.0f - up_fade_value(&l->blink_fade[side], l->now_ms);   /* base light during blinking: off */
        put(out->front[side], k_amber, amber, keep, front_base);
        put(out->rear[side], k_amber, amber, keep, rear_base);
    }
}

uint8_t up_lights_status(const up_lights_t *l)
{
    uint8_t s = l->signal & UP_LIGHTS_SIGNAL_MASK;
    if ((l->blinking[0] || l->blinking[1]) && up_blink_envelope(l->now_ms - l->phase_t0_ms) >= 0.5f) {
        s |= UP_LIGHTS_BLINK_ON;
    }
    if (l->brake) {
        s |= UP_LIGHTS_BRAKE_ON;
    }
    if (l->light_on) {
        s |= UP_LIGHTS_LIGHT_ON;
    }
    return s;
}
