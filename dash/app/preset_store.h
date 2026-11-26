/*
 * Access to the presets stored on core (ADR 0013, 0017), used by the editor API.
 *
 * Implementations: preset_ram.c (demo core, RAM only, like the mock dash of the
 * preset editor) and later the UART link to core (ADR 0004). Slot numbers come
 * from the store; the API does not assume a numbering.
 */
#ifndef PRESET_STORE_H
#define PRESET_STORE_H

#include <stdbool.h>
#include <stdint.h>

#include "up_overlay.h"
#include "up_pattern.h"
#include "up_preset.h"

/* Results (map to the ACK codes of ADR 0004 and to HTTP status codes) */
typedef enum {
    PS_OK = 0,
    PS_CONFLICT,    /* base revision does not match (409) */
    PS_READ_ONLY,   /* built-in preset (403) */
    PS_NO_SLOT,     /* slot does not exist (404) */
    PS_NOT_FOUND,   /* empty slot (404) */
    PS_FULL,        /* no free user slot (507) */
    PS_FAIL,        /* invalid or transfer failed (500/502) */
} ps_result_t;

#define PS_REV_NEW 0xFFFFu   /* base revision for a slot expected to be empty (ADR 0004) */

typedef enum { PS_EMPTY = 0, PS_BUILTIN, PS_USER } ps_kind_t;

typedef struct {
    uint8_t  slot;
    uint8_t  kind;        /* ps_kind_t */
    uint16_t revision;    /* 0 for empty slots */
    uint32_t length_cm;   /* patterns only */
    char     name[UP_PRESET_NAME_BYTES + 1];
} ps_entry_t;

/* One preset in binary form (~12 KB with all segments: allocate on the heap) */
typedef struct {
    uint8_t      category;   /* UP_CAT_PATTERN / UP_CAT_OVERLAY */
    char         name[UP_PRESET_NAME_BYTES + 1];
    up_overlay_t overlay;    /* overlays */
    uint16_t     segment_count;
    up_segment_t segments[UP_PATTERN_MAX_SEGMENTS];
} ps_preset_t;

#define PS_MAX_SLOTS 64   /* per category, enough for built-in + user slots */

typedef struct {
    void *ctx;
    /* Existing built-in slots and all user slots (empty ones included), by slot
       number; returns the number of entries */
    int (*list)(void *ctx, uint8_t cat, ps_entry_t *out, int max);
    uint16_t (*list_rev)(void *ctx);
    ps_result_t (*get)(void *ctx, uint8_t cat, uint8_t slot, ps_preset_t *out, uint16_t *revision);
    /* base_rev: revision the editor started from, PS_REV_NEW for an empty slot */
    ps_result_t (*put)(void *ctx, uint8_t cat, uint8_t slot, uint16_t base_rev, const ps_preset_t *p,
                       uint16_t *new_rev);
    ps_result_t (*del)(void *ctx, uint8_t cat, uint8_t slot);
    /* try on the strip without storing */
    ps_result_t (*preview)(void *ctx, uint8_t cat, const ps_preset_t *p);
} preset_store_t;

#endif /* PRESET_STORE_H */
