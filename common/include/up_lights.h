/*
 * End lights of the two LED strips (ADR 0018): turn signals, hazard lights,
 * brake light, front and tail light – all with smooth transitions.
 *
 * Used by core (render loop) and by the dash simulator. No dynamic memory, no
 * platform calls: the caller passes the time and the speed.
 *
 *   up_lights_init()       defaults
 *   up_lights_set_*()      commands from the dash (ADR 0004: 0x92, 0x93, 0x95)
 *   up_lights_update()     every frame: time, speed, travelled distance
 *   up_lights_output()     linear RGB (0 … 1) for the four ends
 *   up_lights_status()     STATUS byte `lights` (ADR 0004)
 */
#ifndef UP_LIGHTS_H
#define UP_LIGHTS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UP_SIGNAL_OFF    0
#define UP_SIGNAL_LEFT   1
#define UP_SIGNAL_RIGHT  2
#define UP_SIGNAL_HAZARD 3

#define UP_SIDE_LEFT  0
#define UP_SIDE_RIGHT 1

/* STATUS `lights` bits (ADR 0004) */
#define UP_LIGHTS_SIGNAL_MASK 0x03
#define UP_LIGHTS_BLINK_ON    0x04
#define UP_LIGHTS_BRAKE_ON    0x08
#define UP_LIGHTS_LIGHT_ON    0x10

/* Timing (ADR 0018) */
#define UP_BLINK_PERIOD_MS   667   /* 1.5 Hz, 90 flashes per minute */
#define UP_BLINK_RISE_MS     100
#define UP_BLINK_FULL_MS     250
#define UP_BLINK_FALL_MS     150   /* dark for the rest of the period (~167 ms) */
#define UP_SIGNAL_TIMEOUT_MS 30000u
#define UP_SIGNAL_TIMEOUT_MM 10000u
#define UP_BRAKE_RISE_MS     100
#define UP_BRAKE_FALL_MS     200
#define UP_BRAKE_MIN_ON_MS   1000u
#define UP_BRAKE_CONFIRM_MS  50u
#define UP_BRAKE_MIN_SPEED   500     /* mm/s */
#define UP_LIGHT_FADE_MS     200     /* light on/off, start of blinking on an end */
#define UP_TAIL_LEVEL        0.30f   /* tail light: 30 % of the light brightness */

/* A smooth transition between two levels along an S-curve */
typedef struct {
    float from;
    float to;
    uint32_t t0_ms;
    uint16_t dur_ms;
} up_fade_t;

typedef struct {
    /* settings (stored on core, ADR 0018) */
    uint8_t light_brightness;   /* 10 … 100 % */
    bool brake_enabled;
    uint8_t brake_threshold;    /* deceleration in 0.1 m/s², 5 … 30 */

    /* switching state (not stored) */
    uint8_t signal;             /* UP_SIGNAL_* */
    uint32_t signal_t0_ms;      /* for the automatic switch-off */
    uint32_t phase_t0_ms;       /* blink phase; kept when switching sides, so both stay in sync */
    uint32_t signal_t0_mm;      /* travelled distance (total_mm) when switched on */
    bool light_on;

    /* brake detection */
    bool have_speed;
    uint32_t last_ms;
    float speed_smooth;         /* |speed|, mm/s, smoothed ~100 ms */
    float decel;                /* mm/s², smoothed */
    uint32_t above_since_ms;
    bool above;
    bool brake;
    uint32_t brake_on_ms;

    /* smooth levels */
    up_fade_t light_fade;           /* front white / tail red */
    up_fade_t brake_fade;
    up_fade_t blink_fade[2];        /* per side: blinking end replaces the base light */
    float blink_level_off[2];       /* blink level when the side stopped blinking */
    bool blinking[2];

    uint32_t now_ms;
} up_lights_t;

/* Linear RGB 0 … 1 for the four ends */
typedef struct {
    float front[2][3];   /* [side][r, g, b] */
    float rear[2][3];
} up_lights_out_t;

void up_lights_init(up_lights_t *l, uint32_t now_ms);
void up_lights_set_signal(up_lights_t *l, uint8_t signal, uint32_t now_ms, uint32_t total_mm);
void up_lights_set_light(up_lights_t *l, bool on, uint32_t now_ms);
void up_lights_set_settings(up_lights_t *l, uint8_t brightness, bool brake_enabled, uint8_t threshold);

/* speed_mm_s: signed speed (ADR 0015); total_mm: travelled distance (wraps) */
void up_lights_update(up_lights_t *l, uint32_t now_ms, int32_t speed_mm_s, uint32_t total_mm);
void up_lights_output(const up_lights_t *l, up_lights_out_t *out);
uint8_t up_lights_status(const up_lights_t *l);

/* Building blocks, exported for tests */
float up_blink_envelope(uint32_t ms_since_start);   /* 0 … 1 */
float up_fade_value(const up_fade_t *f, uint32_t now_ms);
float up_srgb_to_linear(float c);

#ifdef __cplusplus
}
#endif

#endif /* UP_LIGHTS_H */
