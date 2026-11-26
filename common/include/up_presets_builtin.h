/*
 * Built-in presets (ADR 0013), generated at build time from presets/ in the
 * repository by tools/gen_presets.py. Slot order = file name order.
 */
#ifndef UP_PRESETS_BUILTIN_H
#define UP_PRESETS_BUILTIN_H

#include <stdint.h>

#include "up_overlay.h"
#include "up_pattern.h"

typedef struct {
    const char *name;
    uint16_t segment_count;
    const up_segment_t *segments;
} up_builtin_pattern_t;

typedef struct {
    const char *name;
    up_overlay_t overlay;
} up_builtin_overlay_t;

extern const up_builtin_pattern_t up_builtin_patterns[];
extern const int up_builtin_pattern_count;
extern const up_builtin_overlay_t up_builtin_overlays[];
extern const int up_builtin_overlay_count;

#endif /* UP_PRESETS_BUILTIN_H */
