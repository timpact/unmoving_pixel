/*
 * Unit test for the dash-owned state (ADR 0019 "NVS on the dash"): defaults,
 * loading with range checks, delayed saving of changed values only, RAM-only
 * measurements and ride panel.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dash_state.h"

/* store with three keys in RAM, counts writes */
typedef struct {
    const char *key;
    bool present;
    uint8_t value;
    int writes;
} entry_t;

static entry_t s_entries[3] = {
    {DASH_KEY_DISPLAY_BRIGHTNESS, false, 0, 0},
    {DASH_KEY_MEASURE_DECIMALS, false, 0, 0},
    {DASH_KEY_MEASURE_ADD_BW, false, 0, 0},
};

static entry_t *find(const char *key)
{
    for (int i = 0; i < 3; i++) {
        if (strcmp(s_entries[i].key, key) == 0) return &s_entries[i];
    }
    assert(!"unknown key");
    return NULL;
}

static bool get_u8(void *ctx, const char *key, uint8_t *v)
{
    (void)ctx;
    entry_t *e = find(key);
    if (e->present) *v = e->value;
    return e->present;
}

static void set_u8(void *ctx, const char *key, uint8_t v)
{
    (void)ctx;
    entry_t *e = find(key);
    e->present = true;
    e->value = v;
    e->writes++;
}

static int total_writes(void)
{
    return s_entries[0].writes + s_entries[1].writes + s_entries[2].writes;
}

static void core_set_brightness(void *ctx, uint8_t percent)
{
    (void)ctx;
    (void)percent;
}

static const dash_store_t k_store = {NULL, get_u8, set_u8, NULL, NULL};

/* fake Wi-Fi and a store for the password */
static int s_starts, s_stops;
static bool s_wifi_on;
static char s_started_pw[32];
static char s_pw[32];
static bool s_pw_present;
static uint8_t s_rnd_seed;

static bool wifi_start(void *ctx, const char *ssid, const char *pw)
{
    (void)ctx;
    assert(strcmp(ssid, DASH_WIFI_SSID) == 0);
    snprintf(s_started_pw, sizeof(s_started_pw), "%s", pw);
    s_starts++;
    s_wifi_on = true;
    return true;
}
static void wifi_stop(void *ctx) { (void)ctx; s_stops++; s_wifi_on = false; }
static uint8_t wifi_clients(void *ctx) { (void)ctx; return s_wifi_on ? 1 : 0; }
static void wifi_random(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    for (size_t i = 0; i < len; i++) buf[i] = (uint8_t)(s_rnd_seed + i * 37);
}
static bool get_str(void *ctx, const char *key, char *buf, size_t len)
{
    (void)ctx;
    assert(strcmp(key, DASH_KEY_WIFI_PASSWORD) == 0);
    if (s_pw_present) snprintf(buf, len, "%s", s_pw);
    return s_pw_present;
}
static void set_str(void *ctx, const char *key, const char *v)
{
    (void)ctx;
    assert(strcmp(key, DASH_KEY_WIFI_PASSWORD) == 0);
    snprintf(s_pw, sizeof(s_pw), "%s", v);
    s_pw_present = true;
}
static const dash_store_t k_store_pw = {NULL, get_u8, set_u8, get_str, set_str};
static const dash_wifi_t k_wifi = {NULL, wifi_start, wifi_stop, wifi_clients, wifi_random};

static void test_editor(void)
{
    ui_model_t m;
    ui_actions_t core = {0}, a;

    /* password format: 12 characters, groups of three, no 0/o/1/l */
    uint8_t rnd[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 30, 31, 32};
    char pw[DASH_WIFI_PASSWORD_LEN + 1];
    dash_state_make_password(rnd, pw);
    assert(strcmp(pw, "abc-def-ghi-89a") == 0);

    /* first switch-on generates and stores the password */
    s_pw_present = false;
    s_rnd_seed = 5;
    dash_state_init(&k_store_pw, &k_wifi);
    dash_state_actions(&a, &core);
    dash_state_update(1000, 0, 0);
    dash_state_fill_model(&m);
    assert(!m.editor_on && m.wifi_password[0] == '\0' && m.wifi_clients == 0);
    a.set_editor(NULL, true);
    dash_state_fill_model(&m);
    assert(m.editor_on && s_starts == 1 && s_pw_present && strlen(s_pw) == DASH_WIFI_PASSWORD_LEN);
    assert(strcmp(m.wifi_password, s_pw) == 0 && strcmp(s_started_pw, s_pw) == 0);
    assert(strcmp(m.wifi_ssid, "unmoving-pixel") == 0 && strcmp(m.editor_url, "http://192.168.4.1") == 0);
    assert(m.wifi_clients == 1 && m.editor_remaining_s == 0);

    /* requests keep it on; remaining time shown during the last 2 min; off after 10 min */
    dash_state_update(1000 + 9 * 60000, 0, 0);
    dash_state_editor_activity();
    dash_state_update(1000 + 9 * 60000 + 50, 0, 0);
    dash_state_update(1000 + 17 * 60000, 0, 0);
    dash_state_fill_model(&m);
    assert(m.editor_on && m.editor_remaining_s == 0);
    dash_state_update(1050 + 17 * 60000 + 1000, 0, 0);
    dash_state_fill_model(&m);
    assert(m.editor_on && m.editor_remaining_s == 119);
    dash_state_update(1050 + 19 * 60000, 0, 0);
    dash_state_fill_model(&m);
    assert(!m.editor_on && s_stops == 1 && m.wifi_clients == 0);

    /* restart: same password from the store, no new one */
    char old[32];
    snprintf(old, sizeof(old), "%s", s_pw);
    s_rnd_seed = 99;
    dash_state_init(&k_store_pw, &k_wifi);
    dash_state_actions(&a, &core);
    dash_state_update(0, 0, 0);
    a.set_editor(NULL, true);
    assert(strcmp(s_started_pw, old) == 0);

    /* new password while on: stored, Wi-Fi restarted with it */
    a.new_wifi_password(NULL);
    dash_state_fill_model(&m);
    assert(strcmp(s_pw, old) != 0 && strcmp(s_started_pw, s_pw) == 0 && m.editor_on);
    assert(strcmp(m.wifi_password, s_pw) == 0);
    a.set_editor(NULL, false);
    assert(!s_wifi_on);

    /* an invalid stored password is replaced */
    snprintf(s_pw, sizeof(s_pw), "geheim");
    dash_state_init(&k_store_pw, &k_wifi);
    dash_state_actions(&a, &core);
    a.set_editor(NULL, true);
    assert(strlen(s_pw) == DASH_WIFI_PASSWORD_LEN);

    /* without Wi-Fi the editor stays off */
    dash_state_init(&k_store_pw, NULL);
    dash_state_actions(&a, &core);
    a.set_editor(NULL, true);
    dash_state_fill_model(&m);
    assert(!m.editor_on);
}

int main(void)
{
    ui_model_t m;
    ui_actions_t core = {0}, a;

    /* empty store: defaults, ride panel starts with "Licht", nothing written */
    dash_state_init(&k_store, NULL);
    dash_state_actions(&a, &core);
    dash_state_update(0, 1000, 1000);
    memset(&m, 0, sizeof(m));
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_DEF_DISPLAY_BRIGHTNESS);
    assert(m.measure_decimals == DASH_DEF_MEASURE_DECIMALS);
    assert(!m.measure_add_backward);
    assert(m.ride_panel == UI_RIDE_PANEL_LIGHTS);
    dash_state_update(10000, 1000, 1000);
    assert(total_writes() == 0);

    /* slider: many changes, one write 2 s after the last one */
    for (uint32_t t = 100; t <= 1000; t += 100) {
        dash_state_update(t, 1000, 1000);
        a.set_display_brightness(NULL, (uint8_t)(20 + t / 100));
    }
    dash_state_update(2999, 1000, 1000);
    assert(total_writes() == 0);
    dash_state_update(3000, 1000, 1000);
    assert(s_entries[0].writes == 1 && s_entries[0].value == 30 && total_writes() == 1);

    /* change and change back: nothing to write */
    a.set_measure_decimals(NULL, 0);
    a.set_measure_decimals(NULL, DASH_DEF_MEASURE_DECIMALS);
    dash_state_update(10000, 1000, 1000);
    assert(total_writes() == 1);

    /* out of range is clamped or ignored */
    a.set_display_brightness(NULL, 0);
    a.set_measure_decimals(NULL, 7);
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_MIN_DISPLAY_BRIGHTNESS && m.measure_decimals == DASH_DEF_MEASURE_DECIMALS);

    dash_state_update(15000, 1000, 1000);              /* write the clamped brightness */

    /* measurements and ride panel: RAM only, never written */
    int before = total_writes();
    a.measure_start(NULL, 0);
    dash_state_update(20000, 3000, 3500);              /* net +2500 */
    dash_state_fill_model(&m);
    assert(m.measure_running[0] && m.measure_mm[0] == 2500 && !m.measure_running[1]);
    a.set_measure_add_backward(NULL, true);            /* counter switch: no jump */
    dash_state_fill_model(&m);
    assert(m.measure_mm[0] == 2500 && m.measure_add_backward);
    a.measure_stop(NULL, 0);
    a.set_ride_panel(NULL, UI_RIDE_PANEL_MEASURE);
    dash_state_update(30000, 9000, 9000);
    dash_state_fill_model(&m);
    assert(m.measure_mm[0] == 2500 && !m.measure_running[0] && m.ride_panel == UI_RIDE_PANEL_MEASURE);
    assert(total_writes() == before + 1 && s_entries[2].value == 1);   /* only meas_add_bw */

    /* restart: settings come back, measurements and ride panel do not */
    dash_state_init(&k_store, NULL);
    dash_state_update(0, 9000, 9000);
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_MIN_DISPLAY_BRIGHTNESS);
    assert(m.measure_add_backward);
    assert(m.measure_mm[0] == 0 && !m.measure_running[0]);
    assert(m.ride_panel == UI_RIDE_PANEL_LIGHTS);

    /* stored values out of range give the defaults */
    s_entries[0].value = 200;
    s_entries[1].value = 3;
    s_entries[2].value = 2;
    dash_state_init(&k_store, NULL);
    dash_state_fill_model(&m);
    assert(m.display_brightness == DASH_DEF_DISPLAY_BRIGHTNESS);
    assert(m.measure_decimals == DASH_DEF_MEASURE_DECIMALS && !m.measure_add_backward);

    /* other actions pass through to core */
    core.set_brightness = core_set_brightness;
    dash_state_actions(&a, &core);
    assert(a.set_brightness == core.set_brightness);

    test_editor();
    printf("test_dash_state: OK\n");
    return 0;
}
