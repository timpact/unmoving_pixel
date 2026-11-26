/*
 * Pattern presets (ADR 0011): list of segments with hard edges, repeated along
 * the ground. Shared by core and dash.
 */
#ifndef UP_PATTERN_H
#define UP_PATTERN_H

#include <stdint.h>

#define UP_PATTERN_MAX_SEGMENTS 2048
#define UP_SEGMENT_MIN_CM       1
#define UP_SEGMENT_MAX_CM       65535

/* Preset categories (ADR 0004) */
#define UP_CAT_PATTERN 0
#define UP_CAT_OVERLAY 1

/* Overlay fades: together at most the maximum main part length (ADR 0012, 0018) */
#define UP_OVERLAY_FADE_MAX_MM 1000

/* One segment (ADR 0011; on the wire 6 bytes, little-endian) */
typedef struct {
    uint16_t length_cm;   /* 1 … 65535 */
    uint8_t  brightness;  /* 0 … 100 % */
    uint8_t  r, g, b;     /* sRGB */
} up_segment_t;

#endif /* UP_PATTERN_H */
