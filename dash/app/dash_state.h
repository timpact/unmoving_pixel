/*
 * State owned by the dash (ADR 0019 "NVS on the dash"): dash settings, distance
 * measurements (ADR 0008), the ride panel (ADR 0018) and the preset editor
 * via Wi-Fi (ADR 0013: on/off, password, auto-off).
 *
 * Platform-independent. Settings are loaded and saved through a store passed in
 * by the caller (NVS on the device, RAM in the PC simulator); they are written
 * 2 s after the last change, only the values that changed. Measurements and the
 * ride panel are kept in RAM only. The Wi-Fi password is written at once
 * (generated rarely).
 *
 * All other GUI actions belong to core; dash_state_actions() combines both.
 */
#ifndef DASH_STATE_H
#define DASH_STATE_H

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

#define DASH_SAVE_DELAY_MS 2000u

/* Stored dash settings (one key per value; a missing key or a value out of
 * range gives the default) */
#define DASH_KEY_DISPLAY_BRIGHTNESS "disp_bright"
#define DASH_KEY_MEASURE_DECIMALS   "meas_dec"
#define DASH_KEY_MEASURE_ADD_BW     "meas_add_bw"
#define DASH_KEY_WIFI_PASSWORD      "wifi_pw"

/* Preset editor via Wi-Fi (ADR 0013) */
#define DASH_WIFI_SSID          "unmoving-pixel"
#define DASH_EDITOR_URL         "http://192.168.4.1"
#define DASH_EDITOR_IDLE_MS     (10u * 60u * 1000u)   /* auto-off after the last request */
#define DASH_EDITOR_WARN_MS     (2u * 60u * 1000u)    /* show the remaining time */
#define DASH_WIFI_PASSWORD_LEN  15                    /* "k7q-29v-mn4-x8d" */

#define DASH_DEF_DISPLAY_BRIGHTNESS 70
#define DASH_MIN_DISPLAY_BRIGHTNESS 5
#define DASH_DEF_MEASURE_DECIMALS   2    /* 1 cm */
#define DASH_MAX_MEASURE_DECIMALS   2

typedef struct {
    void *ctx;
    bool (*get_u8)(void *ctx, const char *key, uint8_t *value);   /* false: key missing */
    void (*set_u8)(void *ctx, const char *key, uint8_t value);
    bool (*get_str)(void *ctx, const char *key, char *buf, size_t len);   /* false: key missing */
    void (*set_str)(void *ctx, const char *key, const char *value);
} dash_store_t;

/* Wi-Fi access point and HTTP server (wifi_ap.c on the device, a fake in the simulator) */
typedef struct {
    void *ctx;
    bool (*start)(void *ctx, const char *ssid, const char *password);
    void (*stop)(void *ctx);
    uint8_t (*clients)(void *ctx);
    void (*random)(void *ctx, uint8_t *buf, size_t len);   /* hardware random numbers */
} dash_wifi_t;

/* Load the settings; store may be NULL (defaults, nothing saved); wifi may be
 * NULL (editor cannot be switched on) */
void dash_state_init(const dash_store_t *store, const dash_wifi_t *wifi);

/* An HTTP request of the editor arrived (any task): restarts the auto-off time */
void dash_state_editor_activity(void);

/* Wi-Fi password from 12 random bytes: 12 characters without 0/o and 1/l in
 * groups of three, e.g. "k7q-29v-mn4-x8d"; out holds DASH_WIFI_PASSWORD_LEN + 1 */
void dash_state_make_password(const uint8_t random[12], char *out);

/* Call with every new core status (and regularly): current counters of core
 * (ADR 0004/0015) and a millisecond clock for the delayed save */
void dash_state_update(uint32_t now_ms, uint32_t total_mm, uint32_t position_mm);

/* Fill the dash-owned fields of the model (after the core fields) */
void dash_state_fill_model(ui_model_t *m);

/* out = core actions, with the dash-owned actions replaced by dash_state */
void dash_state_actions(ui_actions_t *out, const ui_actions_t *core);

/* Write pending changes now (e.g. before a restart) */
void dash_state_flush(void);

#endif /* DASH_STATE_H */
