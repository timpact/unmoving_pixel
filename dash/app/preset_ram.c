/* Preset store in RAM for the demo core, see preset_ram.h. */
#include "preset_ram.h"

#include <stdlib.h>
#include <string.h>

#include "up_presets_builtin.h"

typedef struct {
    ps_preset_t *user[PRESET_RAM_PATTERN_USER];   /* NULL = empty */
    uint16_t revision[PRESET_RAM_PATTERN_BUILTIN + PRESET_RAM_PATTERN_USER];  /* current, 0 = empty */
    uint16_t last_rev[PRESET_RAM_PATTERN_BUILTIN + PRESET_RAM_PATTERN_USER];  /* never decreases */
    uint8_t builtin_count;
    uint8_t builtin_slots;
    uint8_t user_slots;
    uint8_t active;
} category_t;

static struct {
    category_t cat[2];
    uint16_t list_rev;
    up_overlay_t overlay;   /* values in effect */
} s;

/* name into a buffer of UP_PRESET_NAME_BYTES + 1, always terminated */
static void copy_name(char *dst, const char *src)
{
    size_t n = strnlen(src, UP_PRESET_NAME_BYTES);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static category_t *category(uint8_t cat)
{
    return cat <= UP_CAT_OVERLAY ? &s.cat[cat] : NULL;
}

static bool is_user(const category_t *c, uint8_t slot)
{
    return slot >= c->builtin_slots && slot < c->builtin_slots + c->user_slots;
}

static bool is_builtin(const category_t *c, uint8_t slot)
{
    return slot < c->builtin_count;
}

static void builtin_preset(uint8_t cat, uint8_t slot, ps_preset_t *p)
{
    memset(p, 0, sizeof(*p) - sizeof(p->segments));
    p->category = cat;
    if (cat == UP_CAT_PATTERN) {
        const up_builtin_pattern_t *b = &up_builtin_patterns[slot];
        copy_name(p->name, b->name);
        p->segment_count = b->segment_count;
        memcpy(p->segments, b->segments, sizeof(up_segment_t) * b->segment_count);
    } else {
        const up_builtin_overlay_t *b = &up_builtin_overlays[slot];
        copy_name(p->name, b->name);
        p->overlay = b->overlay;
    }
}

/* name of an existing preset, or NULL */
static const char *slot_name(uint8_t cat, uint8_t slot)
{
    const category_t *c = &s.cat[cat];
    if (is_builtin(c, slot)) {
        return cat == UP_CAT_PATTERN ? up_builtin_patterns[slot].name : up_builtin_overlays[slot].name;
    }
    if (is_user(c, slot) && c->user[slot - c->builtin_slots] != NULL) {
        return c->user[slot - c->builtin_slots]->name;
    }
    return NULL;
}

static bool overlay_values(uint8_t slot, up_overlay_t *out)
{
    const category_t *c = &s.cat[UP_CAT_OVERLAY];
    if (is_builtin(c, slot)) {
        *out = up_builtin_overlays[slot].overlay;
        return true;
    }
    if (is_user(c, slot) && c->user[slot - c->builtin_slots] != NULL) {
        *out = c->user[slot - c->builtin_slots]->overlay;
        return true;
    }
    return false;
}

void preset_ram_init(void)
{
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < PRESET_RAM_PATTERN_USER; k++) {
            free(s.cat[i].user[k]);
        }
    }
    memset(&s, 0, sizeof(s));
    category_t *p = &s.cat[UP_CAT_PATTERN];
    category_t *o = &s.cat[UP_CAT_OVERLAY];
    p->builtin_slots = PRESET_RAM_PATTERN_BUILTIN;
    p->user_slots = PRESET_RAM_PATTERN_USER;
    p->builtin_count = up_builtin_pattern_count < PRESET_RAM_PATTERN_BUILTIN ? up_builtin_pattern_count
                                                                             : PRESET_RAM_PATTERN_BUILTIN;
    o->builtin_slots = PRESET_RAM_OVERLAY_BUILTIN;
    o->user_slots = PRESET_RAM_OVERLAY_USER;
    o->builtin_count = up_builtin_overlay_count < PRESET_RAM_OVERLAY_BUILTIN ? up_builtin_overlay_count
                                                                             : PRESET_RAM_OVERLAY_BUILTIN;
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < s.cat[i].builtin_count; k++) {
            s.cat[i].revision[k] = s.cat[i].last_rev[k] = 1;
        }
    }
    s.list_rev = 1;
    overlay_values(0, &s.overlay);
}

/* ---- preset_store_t ------------------------------------------------------- */

static int ram_list(void *ctx, uint8_t cat, ps_entry_t *out, int max)
{
    (void)ctx;
    const category_t *c = category(cat);
    int n = 0;
    if (c == NULL) {
        return 0;
    }
    for (int slot = 0; slot < c->builtin_slots + c->user_slots && n < max; slot++) {
        const char *name = slot_name(cat, (uint8_t)slot);
        if (name == NULL && !is_user(c, (uint8_t)slot)) {
            continue;
        }
        ps_entry_t *e = &out[n++];
        memset(e, 0, sizeof(*e));
        e->slot = (uint8_t)slot;
        e->kind = name == NULL ? PS_EMPTY : is_builtin(c, (uint8_t)slot) ? PS_BUILTIN : PS_USER;
        e->revision = c->revision[slot];
        if (name != NULL) {
            copy_name(e->name, name);
        }
        if (name != NULL && cat == UP_CAT_PATTERN) {
            uint16_t count;
            const up_segment_t *seg;
            if (is_builtin(c, (uint8_t)slot)) {
                count = up_builtin_patterns[slot].segment_count;
                seg = up_builtin_patterns[slot].segments;
            } else {
                count = c->user[slot - c->builtin_slots]->segment_count;
                seg = c->user[slot - c->builtin_slots]->segments;
            }
            for (int i = 0; i < count; i++) {
                e->length_cm += seg[i].length_cm;
            }
        }
    }
    return n;
}

static uint16_t ram_list_rev(void *ctx)
{
    (void)ctx;
    return s.list_rev;
}

static ps_result_t ram_get(void *ctx, uint8_t cat, uint8_t slot, ps_preset_t *out, uint16_t *revision)
{
    (void)ctx;
    category_t *c = category(cat);
    if (c == NULL || slot_name(cat, slot) == NULL) {
        return PS_NOT_FOUND;
    }
    if (is_builtin(c, slot)) {
        builtin_preset(cat, slot, out);
    } else {
        memcpy(out, c->user[slot - c->builtin_slots], sizeof(*out));
    }
    *revision = c->revision[slot];
    return PS_OK;
}

/* writes without checks; returns the new revision */
static uint16_t store(uint8_t cat, uint8_t slot, const ps_preset_t *p)
{
    category_t *c = &s.cat[cat];
    ps_preset_t **dst = &c->user[slot - c->builtin_slots];
    if (*dst == NULL) {
        *dst = malloc(sizeof(ps_preset_t));
    }
    if (*dst == NULL) {
        return 0;
    }
    memcpy(*dst, p, sizeof(ps_preset_t));
    (*dst)->category = cat;
    c->revision[slot] = ++c->last_rev[slot];
    s.list_rev++;
    if (cat == UP_CAT_OVERLAY && c->active == slot) {
        s.overlay = p->overlay;
    }
    return c->revision[slot];
}

static ps_result_t ram_put(void *ctx, uint8_t cat, uint8_t slot, uint16_t base_rev, const ps_preset_t *p,
                           uint16_t *new_rev)
{
    (void)ctx;
    category_t *c = category(cat);
    if (c == NULL) {
        return PS_NO_SLOT;
    }
    if (is_builtin(c, slot)) {
        return PS_READ_ONLY;
    }
    if (!is_user(c, slot)) {
        return PS_NO_SLOT;
    }
    uint16_t current = c->user[slot - c->builtin_slots] != NULL ? c->revision[slot] : PS_REV_NEW;
    if (base_rev != current) {
        return PS_CONFLICT;
    }
    uint16_t rev = store(cat, slot, p);
    if (rev == 0) {
        return PS_FAIL;
    }
    *new_rev = rev;
    return PS_OK;
}

static ps_result_t ram_del(void *ctx, uint8_t cat, uint8_t slot)
{
    (void)ctx;
    category_t *c = category(cat);
    if (c == NULL || slot_name(cat, slot) == NULL) {
        return PS_NOT_FOUND;
    }
    if (is_builtin(c, slot)) {
        return PS_READ_ONLY;
    }
    free(c->user[slot - c->builtin_slots]);
    c->user[slot - c->builtin_slots] = NULL;
    c->revision[slot] = 0;
    s.list_rev++;
    if (c->active == slot) {
        preset_ram_select(cat, 0);
    }
    return PS_OK;
}

static ps_result_t ram_preview(void *ctx, uint8_t cat, const ps_preset_t *p)
{
    (void)ctx;
    if (category(cat) == NULL) {
        return PS_NO_SLOT;
    }
    if (cat == UP_CAT_OVERLAY) {
        s.overlay = p->overlay;   /* pattern preview: nothing to show without strip */
    }
    return PS_OK;
}

const preset_store_t preset_ram_store = {
    .ctx = NULL,
    .list = ram_list,
    .list_rev = ram_list_rev,
    .get = ram_get,
    .put = ram_put,
    .del = ram_del,
    .preview = ram_preview,
};

/* ---- actions on the dash ---------------------------------------------------- */

ps_result_t preset_ram_select(uint8_t cat, uint8_t slot)
{
    category_t *c = category(cat);
    if (c == NULL || slot_name(cat, slot) == NULL) {
        return PS_NOT_FOUND;
    }
    c->active = slot;
    if (cat == UP_CAT_OVERLAY) {
        overlay_values(slot, &s.overlay);
    }
    return PS_OK;
}

ps_result_t preset_ram_dash_set_overlay(const up_overlay_t *values, uint8_t *slot_out)
{
    category_t *c = &s.cat[UP_CAT_OVERLAY];
    uint8_t slot = c->active;
    const char *name = slot_name(UP_CAT_OVERLAY, slot);
    if (name == NULL) {
        return PS_NOT_FOUND;
    }
    static ps_preset_t next;   /* overlays need no segments, but the type is large */
    memset(&next, 0, sizeof(next) - sizeof(next.segments));
    next.category = UP_CAT_OVERLAY;
    copy_name(next.name, name);
    next.overlay = *values;
    if (is_builtin(c, slot)) {
        int free_slot = -1;
        const char *names[PRESET_RAM_OVERLAY_BUILTIN + PRESET_RAM_OVERLAY_USER];
        size_t n = 0;
        for (int i = 0; i < c->builtin_slots + c->user_slots; i++) {
            const char *nm = slot_name(UP_CAT_OVERLAY, (uint8_t)i);
            if (nm != NULL) {
                names[n++] = nm;
            } else if (is_user(c, (uint8_t)i) && free_slot < 0) {
                free_slot = i;
            }
        }
        if (free_slot < 0) {
            return PS_FULL;
        }
        up_preset_copy_name(name, names, n, next.name);
        slot = (uint8_t)free_slot;
        c->active = slot;
    }
    if (store(UP_CAT_OVERLAY, slot, &next) == 0) {
        return PS_FAIL;
    }
    s.overlay = *values;
    *slot_out = slot;
    return PS_OK;
}

uint8_t preset_ram_active(uint8_t cat)
{
    const category_t *c = category(cat);
    return c != NULL ? c->active : 0;
}

up_overlay_t preset_ram_overlay(void)
{
    return s.overlay;
}
