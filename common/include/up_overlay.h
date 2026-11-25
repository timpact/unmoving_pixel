/*
 * Strip overlay (ADR 0012): edge fade with two S-curves.
 *
 * Shared by core (rendering) and dash (GUI preview). Header-only, no dependencies.
 */
#ifndef UP_OVERLAY_H
#define UP_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UP_OVERLAY_TYPE_EDGE_FADE 1

/* Active overlay parameters (8 bytes on the wire, ADR 0004). */
typedef struct {
    uint8_t  type;           /* UP_OVERLAY_TYPE_EDGE_FADE */
    uint8_t  level_start;    /* 0 … 100 % at the first LED */
    uint8_t  level_mid;      /* 0 … 100 % in the middle section */
    uint8_t  level_end;      /* 0 … 100 % at the last LED */
    uint16_t fade_start_mm;  /* length of the S-curve at the strip start */
    uint16_t fade_end_mm;    /* length of the S-curve at the strip end */
} up_overlay_t;

static inline float up_smoothstep(float t)
{
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

/*
 * Overlay factor (0.0 … 1.0) at strip position s_mm on a strip of length len_mm.
 * Fade lengths are clamped so that they never overlap.
 */
static inline float up_overlay_factor(const up_overlay_t *o, float s_mm, float len_mm)
{
    float fs = (float)o->fade_start_mm;
    float fe = (float)o->fade_end_mm;
    if (fs + fe > len_mm && fs + fe > 0.0f) {
        float k = len_mm / (fs + fe);
        fs *= k;
        fe *= k;
    }

    float start = o->level_start / 100.0f;
    float mid = o->level_mid / 100.0f;
    float end = o->level_end / 100.0f;

    if (fs > 0.0f && s_mm < fs) {
        return start + (mid - start) * up_smoothstep(s_mm / fs);
    }
    if (fe > 0.0f && s_mm > len_mm - fe) {
        return end + (mid - end) * up_smoothstep((len_mm - s_mm) / fe);
    }
    return mid;
}

#ifdef __cplusplus
}
#endif

#endif /* UP_OVERLAY_H */
