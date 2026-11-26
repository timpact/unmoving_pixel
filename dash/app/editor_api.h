/*
 * HTTP API of the preset editor (ADR 0013, 0017), platform-independent.
 *
 * Takes method, path and body of a request and returns status and JSON body.
 * Same rules, status codes and German messages as the mock dash of the editor
 * (tools/preset-editor/mock-dash.mjs, model.js); both are tested with the same
 * cases (dash/app/test/test_editor_api.c, tools/preset-editor/test/).
 *
 *   GET    /api/state                  settings, list revision, both lists
 *   GET    /api/settings               LED settings
 *   GET    /api/{cat}                  list (cat: patterns, overlays)
 *   GET    /api/{cat}/{slot}           one preset
 *   PUT    /api/{cat}/{slot}           { base_revision, preset } → 409 on conflict
 *   DELETE /api/{cat}/{slot}
 *   POST   /api/preview/{cat}          try a preset without storing
 *   GET    /api/export                 all user presets as bundle
 *   POST   /api/time                   { epoch_ms, tz_offset_min }
 *
 * The caller serialises calls (one lock with the GUI).
 */
#ifndef EDITOR_API_H
#define EDITOR_API_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "preset_store.h"

#define EDITOR_API_MAX_BODY (256 * 1024)

typedef struct {
    uint8_t      global_brightness;
    uint8_t      active_pattern;
    uint8_t      active_overlay;
    up_overlay_t overlay;
    uint8_t      main_leds, front_leds, rear_leds, layout_flags;
    bool         time_valid;
} editor_settings_t;

typedef struct {
    const preset_store_t *store;
    void *ctx;
    /* current settings (from the model) */
    void (*get_settings)(void *ctx, editor_settings_t *out);
    /* clock from the browser: local time in seconds since 1970 (ADR 0014) */
    void (*set_time)(void *ctx, uint32_t local_time);
} editor_api_t;

typedef struct {
    int status;
    char *body;   /* JSON, allocated; free with editor_api_free() */
} editor_response_t;

void editor_api_handle(const editor_api_t *api, const char *method, const char *path, const char *body,
                       size_t body_len, editor_response_t *resp);
void editor_api_free(editor_response_t *resp);

#endif /* EDITOR_API_H */
