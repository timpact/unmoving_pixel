/*
 * Unit test for the editor HTTP API with the RAM preset store (ADR 0013, 0017).
 * The cases mirror tools/preset-editor/test/mock-dash.test.js, so the dash and
 * the mock dash of the editor follow the same rules.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "editor_api.h"
#include "preset_ram.h"
#include "up_presets_builtin.h"

static uint32_t s_time;

static void get_settings(void *ctx, editor_settings_t *out)
{
    (void)ctx;
    memset(out, 0, sizeof(*out));
    out->global_brightness = 80;
    out->active_pattern = preset_ram_active(UP_CAT_PATTERN);
    out->active_overlay = preset_ram_active(UP_CAT_OVERLAY);
    out->overlay = preset_ram_overlay();
    out->main_leds = 72;
    out->front_leds = out->rear_leds = 4;
    out->layout_flags = 3;
    out->time_valid = s_time != 0;
}

static void set_time(void *ctx, uint32_t t)
{
    (void)ctx;
    s_time = t;
}

static const editor_api_t k_api = {&preset_ram_store, NULL, get_settings, set_time};

/* request; returns the parsed answer (caller deletes), status in *status */
static cJSON *call(const char *method, const char *path, const char *body, int *status)
{
    editor_response_t r;
    editor_api_handle(&k_api, method, path, body, body ? strlen(body) : 0, &r);
    *status = r.status;
    cJSON *j = r.body ? cJSON_Parse(r.body) : NULL;
    editor_api_free(&r);
    return j;
}

static int status_of(const char *method, const char *path, const char *body)
{
    int st;
    cJSON_Delete(call(method, path, body, &st));
    return st;
}

static int num(const cJSON *j, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    assert(cJSON_IsNumber(v));
    return v->valueint;
}

static const char *str(const cJSON *j, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(j, key);
    assert(cJSON_IsString(v));
    return v->valuestring;
}

/* newPattern() / newOverlay() of model.js */
static void pattern_json(char *buf, size_t len, const char *name)
{
    snprintf(buf, len,
             "{\"format_version\":1,\"type\":\"pattern\",\"name\":\"%s\",\"segments\":["
             "{\"length_cm\":10,\"color\":\"#2dd4bf\",\"brightness\":100},"
             "{\"length_cm\":10,\"color\":\"#000000\",\"brightness\":0}]}",
             name);
}

static void overlay_json(char *buf, size_t len, const char *name)
{
    snprintf(buf, len,
             "{\"format_version\":1,\"type\":\"overlay\",\"overlay_type\":\"edge_fade\",\"name\":\"%s\","
             "\"level_start\":20,\"level_mid\":100,\"level_end\":20,\"fade_start_mm\":150,\"fade_end_mm\":150}",
             name);
}

static int put(const char *cat, int slot, const char *base, const char *preset, cJSON **answer)
{
    char path[48], body[1024];
    int st;
    snprintf(path, sizeof(path), "/api/%s/%d", cat, slot);
    snprintf(body, sizeof(body), "{\"base_revision\":%s,\"preset\":%s}", base, preset);
    cJSON *j = call("PUT", path, body, &st);
    if (answer) {
        *answer = j;
    } else {
        cJSON_Delete(j);
    }
    return st;
}

int main(void)
{
    char p[1024];
    int st;
    cJSON *j;
    const int PU = PRESET_RAM_PATTERN_BUILTIN, OU = PRESET_RAM_OVERLAY_BUILTIN;

    /* list: built-ins first, then all user slots */
    preset_ram_init();
    j = call("GET", "/api/state", NULL, &st);
    assert(st == 200);
    const cJSON *ovs = cJSON_GetObjectItemCaseSensitive(j, "overlays");
    assert(cJSON_GetArraySize(ovs) == up_builtin_overlay_count + PRESET_RAM_OVERLAY_USER);
    const cJSON *first_empty = cJSON_GetArrayItem(ovs, up_builtin_overlay_count);
    assert(strcmp(str(cJSON_GetArrayItem(ovs, 0), "kind"), "builtin") == 0);
    assert(strcmp(str(first_empty, "kind"), "empty") == 0 && num(first_empty, "slot") == OU);
    assert(cJSON_IsNull(cJSON_GetObjectItemCaseSensitive(first_empty, "revision")));
    assert(num(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "patterns"), 0), "length_cm") == 16);
    assert(num(cJSON_GetObjectItemCaseSensitive(j, "settings"), "main_leds") == 72);
    assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(j, "time_valid")));
    cJSON_Delete(j);

    /* a built-in preset as JSON (presets/patterns/00-stripes.json) */
    j = call("GET", "/api/patterns/0", NULL, &st);
    assert(st == 200 && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "builtin")));
    const cJSON *pre = cJSON_GetObjectItemCaseSensitive(j, "preset");
    assert(strcmp(str(pre, "name"), "Streifen") == 0);
    assert(strcmp(str(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(pre, "segments"), 2), "color"), "#0040ff") == 0);
    cJSON_Delete(j);
    assert(status_of("GET", "/api/patterns/20", NULL) == 404);

    /* built-in presets are read-only */
    pattern_json(p, sizeof(p), "x");
    assert(put("patterns", 0, "1", p, NULL) == 403);
    assert(status_of("DELETE", "/api/patterns/0", NULL) == 403);

    /* revisions and 409 conflict */
    pattern_json(p, sizeof(p), "A");
    assert(put("patterns", PU, "null", p, &j) == 200 && num(j, "slot") == PU && num(j, "revision") == 1);
    cJSON_Delete(j);
    pattern_json(p, sizeof(p), "B");
    assert(put("patterns", PU, "null", p, NULL) == 409);   /* slot no longer empty */
    assert(put("patterns", PU, "1", p, &j) == 200 && num(j, "revision") == 2);
    cJSON_Delete(j);
    pattern_json(p, sizeof(p), "C");
    assert(put("patterns", PU, "1", p, &j) == 409);
    assert(num(j, "revision") == 2);
    assert(strcmp(str(cJSON_GetObjectItemCaseSensitive(j, "preset"), "name"), "B") == 0);
    cJSON_Delete(j);
    assert(put("patterns", PU, "\"2\"", p, NULL) == 409);   /* not a number: never matches */

    /* invalid presets are rejected */
    assert(put("patterns", PU + 1, "null",
               "{\"type\":\"pattern\",\"name\":\"x\",\"segments\":[]}", &j) == 400);
    assert(strcmp(str(j, "error"), "Mindestens ein Abschnitt nötig") == 0);
    cJSON_Delete(j);
    overlay_json(p, sizeof(p), "x");
    assert(put("patterns", PU + 1, "null", p, &j) == 400);
    assert(strcmp(str(j, "error"), "Kein Muster") == 0);
    cJSON_Delete(j);
    assert(put("patterns", PU + 1, "null",
               "{\"type\":\"pattern\",\"name\":\"Zu langer Name 1234\",\"segments\":["
               "{\"length_cm\":0,\"color\":\"red\",\"brightness\":101}]}", &j) == 400);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(j, "errors")) == 4);
    assert(strcmp(str(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j, "errors"), 1), "path"),
                  "segments[0].length_cm") == 0);
    cJSON_Delete(j);
    assert(put("overlays", OU, "null",
               "{\"type\":\"overlay\",\"name\":\"x\",\"level_start\":0,\"level_mid\":100,\"level_end\":0,"
               "\"fade_start_mm\":600,\"fade_end_mm\":500}", &j) == 400);
    assert(strcmp(str(j, "error"), "Übergänge zusammen max. 1.000 mm") == 0);
    cJSON_Delete(j);
    assert(put("patterns", PU + 1, "null", "{\"type\":\"pattern\",\"name\":\"x\",\"segments\":[{\"length_cm\":1.5,"
               "\"color\":\" #ABCDEF \",\"brightness\":5}]}", NULL) == 400);   /* length not an integer */
    assert(put("patterns", PU + 1, "null", "{\"type\":\"pattern\",\"name\":\"Ä\",\"segments\":[{\"length_cm\":1,"
               "\"color\":\" #ABCDEF \",\"brightness\":5}]}", NULL) == 200);   /* colour normalised */
    j = call("GET", "/api/patterns/17", NULL, &st);
    assert(strcmp(str(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "preset"),
                                                                           "segments"), 0), "color"), "#abcdef") == 0);
    cJSON_Delete(j);
    assert(status_of("PUT", "/api/patterns/16", "{kein json") == 400);

    /* revision stays monotonic after delete and re-create */
    overlay_json(p, sizeof(p), "A");
    assert(put("overlays", OU, "null", p, NULL) == 200);
    assert(status_of("DELETE", "/api/overlays/8", NULL) == 200);
    assert(status_of("DELETE", "/api/overlays/8", NULL) == 404);
    overlay_json(p, sizeof(p), "B");
    assert(put("overlays", OU, "null", p, &j) == 200 && num(j, "revision") == 2);
    cJSON_Delete(j);

    /* dash slider on a built-in overlay creates "<name> (2)" and selects it */
    preset_ram_init();
    assert(preset_ram_select(UP_CAT_OVERLAY, 2) == PS_OK);   /* Spotlight */
    up_overlay_t v = preset_ram_overlay();
    v.level_mid = 70;
    uint8_t slot;
    assert(preset_ram_dash_set_overlay(&v, &slot) == PS_OK && slot == OU);
    j = call("GET", "/api/overlays/8", NULL, &st);
    assert(strcmp(str(cJSON_GetObjectItemCaseSensitive(j, "preset"), "name"), "Spotlight (2)") == 0);
    assert(num(cJSON_GetObjectItemCaseSensitive(j, "preset"), "level_mid") == 70);
    cJSON_Delete(j);
    assert(preset_ram_active(UP_CAT_OVERLAY) == OU);
    j = call("GET", "/api/overlays/2", NULL, &st);
    assert(num(cJSON_GetObjectItemCaseSensitive(j, "preset"), "level_mid") == 100);   /* built-in unchanged */
    cJSON_Delete(j);
    v.level_mid = 60;   /* next change goes into the copy, no second copy */
    assert(preset_ram_dash_set_overlay(&v, &slot) == PS_OK && slot == OU);
    j = call("GET", "/api/overlays/8", NULL, &st);
    assert(num(j, "revision") == 2);
    cJSON_Delete(j);

    /* dash slider without free slot keeps the built-in values */
    preset_ram_init();
    for (int i = 0; i < PRESET_RAM_OVERLAY_USER; i++) {
        char name[8];
        snprintf(name, sizeof(name), "U%d", i);
        overlay_json(p, sizeof(p), name);
        assert(put("overlays", OU + i, "null", p, NULL) == 200);
    }
    preset_ram_select(UP_CAT_OVERLAY, 0);
    v = preset_ram_overlay();
    v.level_mid = 50;
    assert(preset_ram_dash_set_overlay(&v, &slot) == PS_FULL);
    assert(preset_ram_overlay().level_mid == 100);

    /* deleting the active preset falls back to slot 0 */
    preset_ram_init();
    pattern_json(p, sizeof(p), "A");
    assert(put("patterns", PU, "null", p, NULL) == 200);
    assert(preset_ram_select(UP_CAT_PATTERN, PU) == PS_OK);
    assert(status_of("DELETE", "/api/patterns/16", NULL) == 200);
    assert(preset_ram_active(UP_CAT_PATTERN) == 0);

    /* export contains only user presets */
    overlay_json(p, sizeof(p), "Mein");
    assert(put("overlays", OU, "null", p, NULL) == 200);
    j = call("GET", "/api/export", NULL, &st);
    const cJSON *presets = cJSON_GetObjectItemCaseSensitive(j, "presets");
    assert(st == 200 && strcmp(str(j, "type"), "bundle") == 0 && cJSON_GetArraySize(presets) == 1);
    assert(strcmp(str(cJSON_GetArrayItem(presets, 0), "name"), "Mein") == 0);
    cJSON_Delete(j);

    /* preview of an overlay changes the overlay in effect, not the preset */
    assert(status_of("POST", "/api/preview/overlays",
                     "{\"type\":\"overlay\",\"name\":\"x\",\"level_start\":0,\"level_mid\":42,\"level_end\":0,"
                     "\"fade_start_mm\":0,\"fade_end_mm\":0}") == 200);
    assert(preset_ram_overlay().level_mid == 42);
    assert(status_of("POST", "/api/preview/patterns", "{\"type\":\"overlay\"}") == 400);

    /* time from the browser: local time = UTC + offset */
    assert(status_of("POST", "/api/time", "{\"tz_offset_min\":60}") == 400);
    assert(status_of("POST", "/api/time", "{\"epoch_ms\":1795000000000,\"tz_offset_min\":60}") == 200);
    assert(s_time == 1795000000u + 3600u);

    /* routes */
    assert(status_of("GET", "/api/unknown", NULL) == 404);
    assert(status_of("GET", "/api/patterns/x", NULL) == 404);
    assert(status_of("POST", "/api/state", "{}") == 405);
    assert(status_of("GET", "/api/settings", NULL) == 200);
    assert(status_of("GET", "/api/overlays", NULL) == 200);
    assert(status_of("GET", "/api/state?x=1", NULL) == 200);

    /* a pattern with 2048 segments (largest body, ~120 KB) */
    size_t cap = 200000;
    char *big = malloc(cap);
    size_t n = (size_t)snprintf(big, cap, "{\"base_revision\":null,\"preset\":{\"type\":\"pattern\",\"name\":\"Lang\",\"segments\":[");
    for (int i = 0; i < UP_PATTERN_MAX_SEGMENTS; i++) {
        n += (size_t)snprintf(big + n, cap - n, "%s{\"length_cm\":%d,\"color\":\"#102030\",\"brightness\":50}",
                              i ? "," : "", 1 + i % 100);
    }
    snprintf(big + n, cap - n, "]}}");
    assert(status_of("PUT", "/api/patterns/18", big) == 200);
    j = call("GET", "/api/patterns/18", NULL, &st);
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(j, "preset"),
                                                               "segments")) == UP_PATTERN_MAX_SEGMENTS);
    cJSON_Delete(j);
    free(big);

    preset_ram_init();   /* frees the user presets (leak check) */
    printf("test_editor_api: OK\n");
    return 0;
}
