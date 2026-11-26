/* HTTP API of the preset editor, see editor_api.h. */
#include "editor_api.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

/* ---- responses ------------------------------------------------------------- */

static void respond(editor_response_t *r, int status, cJSON *json)
{
    r->status = status;
    r->body = json != NULL ? cJSON_PrintUnformatted(json) : NULL;
    cJSON_Delete(json);
}

static void fail(editor_response_t *r, int status, const char *error)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", error);
    respond(r, status, j);
}

void editor_api_free(editor_response_t *resp)
{
    cJSON_free(resp->body);
    resp->body = NULL;
}

/* ---- validation (model.js: validatePattern, validateOverlay) --------------- */

typedef struct {
    cJSON *list;      /* [{path, message}] */
    char text[256];   /* messages joined with "; " (shortened) */
    int count;
} errors_t;

static void add_error(errors_t *e, const char *path, const char *message)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "path", path);
    cJSON_AddStringToObject(o, "message", message);
    cJSON_AddItemToArray(e->list, o);
    size_t len = strlen(e->text);
    if (len + strlen(message) + 3 < sizeof(e->text)) {
        snprintf(e->text + len, sizeof(e->text) - len, "%s%s", e->count > 0 ? "; " : "", message);
    }
    e->count++;
}

static bool is_int(const cJSON *v, double min, double max)
{
    return cJSON_IsNumber(v) && v->valuedouble == floor(v->valuedouble) && v->valuedouble >= min &&
           v->valuedouble <= max;
}

static bool is_blank(const char *s)
{
    for (; *s != '\0'; s++) {
        if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') {
            return false;
        }
    }
    return true;
}

static void check_name(const cJSON *preset, errors_t *e, ps_preset_t *out)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(preset, "name");
    if (!cJSON_IsString(name) || is_blank(name->valuestring)) {
        add_error(e, "name", "Name fehlt");
    } else if (strlen(name->valuestring) > UP_PRESET_NAME_BYTES) {
        add_error(e, "name", "Name zu lang (max. 16 Byte)");
    } else {
        strcpy(out->name, name->valuestring);
    }
}

/* "#rrggbb" or "rrggbb", any case, surrounding blanks allowed (normalizeColor) */
static bool parse_color(const cJSON *v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if (!cJSON_IsString(v)) {
        return false;
    }
    const char *s = v->valuestring;
    while (*s == ' ') s++;
    if (*s == '#') s++;
    unsigned rgb = 0;
    for (int i = 0; i < 6; i++) {
        char c = s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) {
            return false;
        }
        rgb = rgb << 4 | (unsigned)d;
    }
    for (s += 6; *s == ' '; s++) {
    }
    if (*s != '\0') {
        return false;
    }
    *r = (uint8_t)(rgb >> 16);
    *g = (uint8_t)(rgb >> 8);
    *b = (uint8_t)rgb;
    return true;
}

static void check_pattern(const cJSON *p, errors_t *e, ps_preset_t *out)
{
    check_name(p, e, out);
    const cJSON *segs = cJSON_GetObjectItemCaseSensitive(p, "segments");
    int n = cJSON_GetArraySize(segs);
    if (!cJSON_IsArray(segs) || n < 1) {
        add_error(e, "segments", "Mindestens ein Abschnitt nötig");
        return;
    }
    if (n > UP_PATTERN_MAX_SEGMENTS) {
        add_error(e, "segments", "Höchstens 2048 Abschnitte");
    }
    int i = 0;
    const cJSON *s;
    cJSON_ArrayForEach(s, segs) {
        char path[48];
        up_segment_t seg = {0};
        const cJSON *len = cJSON_GetObjectItemCaseSensitive(s, "length_cm");
        const cJSON *bright = cJSON_GetObjectItemCaseSensitive(s, "brightness");
        if (!is_int(len, UP_SEGMENT_MIN_CM, UP_SEGMENT_MAX_CM)) {
            snprintf(path, sizeof(path), "segments[%d].length_cm", i);
            add_error(e, path, "Länge 1 … 65535 cm");
        } else {
            seg.length_cm = (uint16_t)len->valuedouble;
        }
        if (!parse_color(cJSON_GetObjectItemCaseSensitive(s, "color"), &seg.r, &seg.g, &seg.b)) {
            snprintf(path, sizeof(path), "segments[%d].color", i);
            add_error(e, path, "Farbe als #rrggbb");
        }
        if (!is_int(bright, 0, 100)) {
            snprintf(path, sizeof(path), "segments[%d].brightness", i);
            add_error(e, path, "Helligkeit 0 … 100 %");
        } else {
            seg.brightness = (uint8_t)bright->valuedouble;
        }
        if (i < UP_PATTERN_MAX_SEGMENTS) {
            out->segments[i] = seg;
        }
        i++;
    }
    out->segment_count = (uint16_t)(n < UP_PATTERN_MAX_SEGMENTS ? n : UP_PATTERN_MAX_SEGMENTS);
}

static void check_overlay(const cJSON *o, errors_t *e, ps_preset_t *out)
{
    static const char *const levels[] = {"level_start", "level_mid", "level_end"};
    static const char *const fades[] = {"fade_start_mm", "fade_end_mm"};
    uint8_t lv[3] = {0};
    uint16_t fd[2] = {0};
    check_name(o, e, out);
    for (int i = 0; i < 3; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, levels[i]);
        if (!is_int(v, 0, 100)) {
            add_error(e, levels[i], "Pegel 0 … 100 %");
        } else {
            lv[i] = (uint8_t)v->valuedouble;
        }
    }
    for (int i = 0; i < 2; i++) {
        const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, fades[i]);
        if (!is_int(v, 0, UP_OVERLAY_FADE_MAX_MM)) {
            add_error(e, fades[i], "Übergang 0 … 1.000 mm");
        } else {
            fd[i] = (uint16_t)v->valuedouble;
        }
    }
    const cJSON *fs = cJSON_GetObjectItemCaseSensitive(o, "fade_start_mm");
    const cJSON *fe = cJSON_GetObjectItemCaseSensitive(o, "fade_end_mm");
    if (is_int(fs, 0, 65535) && is_int(fe, 0, 65535) &&
        fs->valuedouble + fe->valuedouble > UP_OVERLAY_FADE_MAX_MM) {
        add_error(e, "fade_end_mm", "Übergänge zusammen max. 1.000 mm");
    }
    out->overlay = (up_overlay_t){UP_OVERLAY_TYPE_EDGE_FADE, lv[0], lv[1], lv[2], fd[0], fd[1]};
}

/* Converts and validates; on errors answers 400 and returns false */
static bool parse_preset(uint8_t cat, const cJSON *json, ps_preset_t *out, editor_response_t *r)
{
    errors_t e = {.list = cJSON_CreateArray()};
    memset(out, 0, sizeof(*out) - sizeof(out->segments));
    out->category = cat;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(json, "type");
    const char *want = cat == UP_CAT_PATTERN ? "pattern" : "overlay";
    if (!cJSON_IsString(type) || strcmp(type->valuestring, want) != 0) {
        add_error(&e, "type", cat == UP_CAT_PATTERN ? "Kein Muster" : "Kein Overlay");
    } else if (cat == UP_CAT_PATTERN) {
        check_pattern(json, &e, out);
    } else {
        check_overlay(json, &e, out);
    }
    if (e.count == 0) {
        cJSON_Delete(e.list);
        return true;
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "error", e.text);
    cJSON_AddItemToObject(j, "errors", e.list);
    respond(r, 400, j);
    return false;
}

/* ---- JSON output ------------------------------------------------------------- */

static cJSON *preset_json(const ps_preset_t *p)
{
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "format_version", 1);
    if (p->category == UP_CAT_PATTERN) {
        cJSON_AddStringToObject(j, "type", "pattern");
        cJSON_AddStringToObject(j, "name", p->name);
        cJSON *segs = cJSON_AddArrayToObject(j, "segments");
        for (int i = 0; i < p->segment_count; i++) {
            const up_segment_t *s = &p->segments[i];
            char color[8];
            snprintf(color, sizeof(color), "#%02x%02x%02x", s->r, s->g, s->b);
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "length_cm", s->length_cm);
            cJSON_AddStringToObject(o, "color", color);
            cJSON_AddNumberToObject(o, "brightness", s->brightness);
            cJSON_AddItemToArray(segs, o);
        }
    } else {
        cJSON_AddStringToObject(j, "type", "overlay");
        cJSON_AddStringToObject(j, "overlay_type", "edge_fade");
        cJSON_AddStringToObject(j, "name", p->name);
        cJSON_AddNumberToObject(j, "level_start", p->overlay.level_start);
        cJSON_AddNumberToObject(j, "level_mid", p->overlay.level_mid);
        cJSON_AddNumberToObject(j, "level_end", p->overlay.level_end);
        cJSON_AddNumberToObject(j, "fade_start_mm", p->overlay.fade_start_mm);
        cJSON_AddNumberToObject(j, "fade_end_mm", p->overlay.fade_end_mm);
    }
    return j;
}

static cJSON *list_json(const editor_api_t *api, uint8_t cat)
{
    static ps_entry_t entries[PS_MAX_SLOTS];
    static const char *const kinds[] = {"empty", "builtin", "user"};
    int n = api->store->list(api->store->ctx, cat, entries, PS_MAX_SLOTS);
    cJSON *a = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        const ps_entry_t *e = &entries[i];
        cJSON *o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "slot", e->slot);
        cJSON_AddStringToObject(o, "kind", kinds[e->kind <= PS_USER ? e->kind : 0]);
        if (e->kind == PS_EMPTY) {
            cJSON_AddNullToObject(o, "revision");
        } else {
            cJSON_AddNumberToObject(o, "revision", e->revision);
        }
        cJSON_AddStringToObject(o, "name", e->name);
        cJSON_AddNumberToObject(o, "length_cm", e->length_cm);
        cJSON_AddItemToArray(a, o);
    }
    return a;
}

static cJSON *settings_json(const editor_api_t *api)
{
    editor_settings_t s;
    api->get_settings(api->ctx, &s);
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "global_brightness", s.global_brightness);
    cJSON_AddNumberToObject(j, "active_pattern", s.active_pattern);
    cJSON_AddNumberToObject(j, "active_overlay", s.active_overlay);
    cJSON *o = cJSON_AddObjectToObject(j, "overlay");
    cJSON_AddNumberToObject(o, "level_start", s.overlay.level_start);
    cJSON_AddNumberToObject(o, "level_mid", s.overlay.level_mid);
    cJSON_AddNumberToObject(o, "level_end", s.overlay.level_end);
    cJSON_AddNumberToObject(o, "fade_start_mm", s.overlay.fade_start_mm);
    cJSON_AddNumberToObject(o, "fade_end_mm", s.overlay.fade_end_mm);
    cJSON_AddNumberToObject(j, "main_leds", s.main_leds);
    cJSON_AddNumberToObject(j, "front_leds", s.front_leds);
    cJSON_AddNumberToObject(j, "rear_leds", s.rear_leds);
    cJSON_AddNumberToObject(j, "layout_flags", s.layout_flags);
    return j;
}

/* ---- routes ------------------------------------------------------------------ */

static int category_of(const char *name, size_t len)
{
    if (len == 8 && strncmp(name, "patterns", 8) == 0) return UP_CAT_PATTERN;
    if (len == 8 && strncmp(name, "overlays", 8) == 0) return UP_CAT_OVERLAY;
    return -1;
}

/* slot number 0 … 255 from the path, -1 if not a number */
static int slot_of(const char *s)
{
    if (*s == '\0' || strlen(s) > 3) {
        return -1;
    }
    int v = 0;
    for (; *s != '\0'; s++) {
        if (*s < '0' || *s > '9') {
            return -1;
        }
        v = v * 10 + (*s - '0');
    }
    return v <= 255 ? v : -1;
}

static const ps_entry_t *find_entry(const editor_api_t *api, uint8_t cat, uint8_t slot)
{
    static ps_entry_t entries[PS_MAX_SLOTS];
    int n = api->store->list(api->store->ctx, cat, entries, PS_MAX_SLOTS);
    for (int i = 0; i < n; i++) {
        if (entries[i].slot == slot) {
            return &entries[i];
        }
    }
    return NULL;
}

static void get_preset(const editor_api_t *api, uint8_t cat, uint8_t slot, editor_response_t *r)
{
    ps_preset_t *p = malloc(sizeof(*p));
    uint16_t rev;
    if (p == NULL) {
        fail(r, 500, "Kein Speicher");
        return;
    }
    const ps_entry_t *e = find_entry(api, cat, slot);
    if (e == NULL || e->kind == PS_EMPTY || api->store->get(api->store->ctx, cat, slot, p, &rev) != PS_OK) {
        fail(r, 404, "Preset nicht gefunden");
    } else {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "slot", slot);
        cJSON_AddBoolToObject(j, "builtin", e->kind == PS_BUILTIN);
        cJSON_AddNumberToObject(j, "revision", rev);
        cJSON_AddItemToObject(j, "preset", preset_json(p));
        respond(r, 200, j);
    }
    free(p);
}

static void put_preset(const editor_api_t *api, uint8_t cat, uint8_t slot, const cJSON *body, editor_response_t *r)
{
    const ps_entry_t *e = find_entry(api, cat, slot);
    if (e != NULL && e->kind == PS_BUILTIN) {
        fail(r, 403, "Eingebaute Presets können nicht überschrieben werden");
        return;
    }
    if (e == NULL) {
        fail(r, 404, "Slot existiert nicht");
        return;
    }
    ps_preset_t *p = malloc(sizeof(*p));
    if (p == NULL) {
        fail(r, 500, "Kein Speicher");
        return;
    }
    if (!parse_preset(cat, cJSON_GetObjectItemCaseSensitive(body, "preset"), p, r)) {
        free(p);
        return;
    }
    /* base_revision: number, or null/missing for an empty slot; anything else
       never matches (like the mock dash) and is answered as conflict */
    const cJSON *base = cJSON_GetObjectItemCaseSensitive(body, "base_revision");
    uint16_t rev = 0;
    ps_result_t res;
    if (base == NULL || cJSON_IsNull(base)) {
        res = api->store->put(api->store->ctx, cat, slot, PS_REV_NEW, p, &rev);
    } else if (is_int(base, 1, PS_REV_NEW - 1)) {
        res = api->store->put(api->store->ctx, cat, slot, (uint16_t)base->valuedouble, p, &rev);
    } else {
        res = PS_CONFLICT;
    }
    if (res == PS_OK) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "slot", slot);
        cJSON_AddNumberToObject(j, "revision", rev);
        respond(r, 200, j);
    } else if (res == PS_CONFLICT) {
        uint16_t cur_rev;
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "error", "Preset wurde inzwischen geändert");
        if (api->store->get(api->store->ctx, cat, slot, p, &cur_rev) == PS_OK) {
            cJSON_AddNumberToObject(j, "revision", cur_rev);
            cJSON_AddItemToObject(j, "preset", preset_json(p));
        } else {
            cJSON_AddNullToObject(j, "revision");
            cJSON_AddNullToObject(j, "preset");
        }
        respond(r, 409, j);
    } else if (res == PS_READ_ONLY) {
        fail(r, 403, "Eingebaute Presets können nicht überschrieben werden");
    } else if (res == PS_NO_SLOT) {
        fail(r, 404, "Slot existiert nicht");
    } else {
        fail(r, 502, "core hat das Preset nicht gespeichert");
    }
    free(p);
}

static void delete_preset(const editor_api_t *api, uint8_t cat, uint8_t slot, editor_response_t *r)
{
    ps_result_t res = api->store->del(api->store->ctx, cat, slot);
    if (res == PS_OK) {
        cJSON *j = cJSON_CreateObject();
        cJSON_AddNumberToObject(j, "slot", slot);
        respond(r, 200, j);
    } else if (res == PS_READ_ONLY) {
        fail(r, 403, "Eingebaute Presets können nicht gelöscht werden");
    } else if (res == PS_NOT_FOUND || res == PS_NO_SLOT) {
        fail(r, 404, "Preset nicht gefunden");
    } else {
        fail(r, 502, "core hat das Preset nicht gelöscht");
    }
}

static void preview(const editor_api_t *api, uint8_t cat, const cJSON *body, editor_response_t *r)
{
    ps_preset_t *p = malloc(sizeof(*p));
    if (p == NULL) {
        fail(r, 500, "Kein Speicher");
        return;
    }
    if (parse_preset(cat, body, p, r)) {
        if (api->store->preview(api->store->ctx, cat, p) == PS_OK) {
            respond(r, 200, cJSON_CreateObject());
        } else {
            fail(r, 502, "core hat die Vorschau nicht übernommen");
        }
    }
    free(p);
}

static void export_all(const editor_api_t *api, editor_response_t *r)
{
    static ps_entry_t entries[PS_MAX_SLOTS];
    ps_preset_t *p = malloc(sizeof(*p));
    if (p == NULL) {
        fail(r, 500, "Kein Speicher");
        return;
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddNumberToObject(j, "format_version", 1);
    cJSON_AddStringToObject(j, "type", "bundle");
    cJSON *presets = cJSON_AddArrayToObject(j, "presets");
    for (uint8_t cat = UP_CAT_PATTERN; cat <= UP_CAT_OVERLAY; cat++) {
        int n = api->store->list(api->store->ctx, cat, entries, PS_MAX_SLOTS);
        for (int i = 0; i < n; i++) {
            uint16_t rev;
            if (entries[i].kind == PS_USER &&
                api->store->get(api->store->ctx, cat, entries[i].slot, p, &rev) == PS_OK) {
                cJSON_AddItemToArray(presets, preset_json(p));
            }
        }
    }
    free(p);
    respond(r, 200, j);
}

static void set_time(const editor_api_t *api, const cJSON *body, editor_response_t *r)
{
    const cJSON *ms = cJSON_GetObjectItemCaseSensitive(body, "epoch_ms");
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(body, "tz_offset_min");
    if (!cJSON_IsNumber(ms) || !isfinite(ms->valuedouble) || ms->valuedouble < 0) {
        fail(r, 400, "Zeit fehlt");
        return;
    }
    double local = ms->valuedouble / 1000.0 + (is_int(tz, -24 * 60, 24 * 60) ? tz->valuedouble * 60 : 0);
    if (api->set_time != NULL && local >= 0 && local < 4294967296.0) {
        api->set_time(api->ctx, (uint32_t)local);
    }
    respond(r, 200, cJSON_CreateObject());
}

void editor_api_handle(const editor_api_t *api, const char *method, const char *path, const char *body,
                       size_t body_len, editor_response_t *r)
{
    bool get = strcmp(method, "GET") == 0;
    bool put = strcmp(method, "PUT") == 0;
    bool post = strcmp(method, "POST") == 0;
    bool del = strcmp(method, "DELETE") == 0;
    r->status = 0;
    r->body = NULL;

    if (strncmp(path, "/api/", 5) != 0) {
        fail(r, 404, "Unbekannter Pfad");
        return;
    }
    const char *p = path + 5;
    const char *q = strchr(p, '?');
    size_t plen = q != NULL ? (size_t)(q - p) : strlen(p);
    char route[64];
    if (plen >= sizeof(route)) {
        fail(r, 404, "Unbekannter Pfad");
        return;
    }
    memcpy(route, p, plen);
    route[plen] = '\0';

    cJSON *json = NULL;
    if (put || post) {
        if (body_len > EDITOR_API_MAX_BODY) {
            fail(r, 413, "Anfrage zu groß");
            return;
        }
        json = cJSON_ParseWithLength(body != NULL ? body : "", body_len);
        if (json == NULL) {
            fail(r, 400, "Keine gültige JSON-Anfrage");
            return;
        }
    }

    char *slash = strchr(route, '/');
    if (strcmp(route, "state") == 0) {
        if (!get) goto not_allowed;
        cJSON *j = cJSON_CreateObject();
        editor_settings_t s;
        api->get_settings(api->ctx, &s);
        cJSON_AddItemToObject(j, "settings", settings_json(api));
        cJSON_AddNumberToObject(j, "preset_list_rev", api->store->list_rev(api->store->ctx));
        cJSON_AddBoolToObject(j, "time_valid", s.time_valid);
        cJSON_AddItemToObject(j, "patterns", list_json(api, UP_CAT_PATTERN));
        cJSON_AddItemToObject(j, "overlays", list_json(api, UP_CAT_OVERLAY));
        respond(r, 200, j);
    } else if (strcmp(route, "settings") == 0) {
        if (!get) goto not_allowed;
        respond(r, 200, settings_json(api));
    } else if (strcmp(route, "export") == 0) {
        if (!get) goto not_allowed;
        export_all(api, r);
    } else if (strcmp(route, "time") == 0) {
        if (!post) goto not_allowed;
        set_time(api, json, r);
    } else if (strncmp(route, "preview/", 8) == 0) {
        int cat = category_of(route + 8, strlen(route + 8));
        if (cat < 0) {
            fail(r, 404, "Unbekannte Kategorie");
        } else if (!post) {
            goto not_allowed;
        } else {
            preview(api, (uint8_t)cat, json, r);
        }
    } else {
        int cat = category_of(route, slash != NULL ? (size_t)(slash - route) : strlen(route));
        if (cat < 0) {
            fail(r, 404, "Unbekannter Pfad");
        } else if (slash == NULL) {
            if (!get) goto not_allowed;
            respond(r, 200, list_json(api, (uint8_t)cat));
        } else {
            int slot = slot_of(slash + 1);
            if (slot < 0) {
                fail(r, 404, "Slot existiert nicht");
            } else if (get) {
                get_preset(api, (uint8_t)cat, (uint8_t)slot, r);
            } else if (put) {
                put_preset(api, (uint8_t)cat, (uint8_t)slot, json, r);
            } else if (del) {
                delete_preset(api, (uint8_t)cat, (uint8_t)slot, r);
            } else {
                goto not_allowed;
            }
        }
    }
    cJSON_Delete(json);
    return;

not_allowed:
    cJSON_Delete(json);
    fail(r, 405, "Methode nicht erlaubt");
}
