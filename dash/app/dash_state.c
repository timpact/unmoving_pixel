/* State owned by the dash, see dash_state.h. */
#include "dash_state.h"

#include <stdio.h>
#include <string.h>

#include "measurement.h"

typedef struct {
    uint8_t display_brightness;
    uint8_t measure_decimals;
    uint8_t measure_add_backward;
} settings_t;

static struct {
    const dash_store_t *store;
    const dash_wifi_t *wifi;
    /* preset editor via Wi-Fi (ADR 0013) */
    bool editor_on;
    char wifi_password[DASH_WIFI_PASSWORD_LEN + 1];
    uint32_t editor_activity_ms;
    uint32_t requests_seen;
    settings_t cur;
    settings_t saved;
    bool dirty;
    uint32_t changed_ms;
    uint32_t now_ms;
    uint32_t total_mm;
    uint32_t position_mm;
    measurement_t meas[UI_MEASURE_COUNT];
    uint8_t ride_panel;
} s;

static uint8_t load(const char *key, uint8_t min, uint8_t max, uint8_t def)
{
    uint8_t v;
    if (s.store == NULL || !s.store->get_u8(s.store->ctx, key, &v) || v < min || v > max) {
        return def;
    }
    return v;
}

/* requests counted by the HTTP task; read by the GUI task in dash_state_update */
static volatile uint32_t s_requests;

void dash_state_editor_activity(void)
{
    s_requests++;
}

void dash_state_make_password(const uint8_t random[12], char *out)
{
    static const char alphabet[] = "abcdefghijkmnpqrstuvwxyz23456789";   /* 32, no 0/o/1/l */
    int k = 0;
    for (int i = 0; i < 12; i++) {
        if (i > 0 && i % 3 == 0) {
            out[k++] = '-';
        }
        out[k++] = alphabet[random[i] & 31];
    }
    out[k] = '\0';
}

/* a stored password has the generated form (anything else is replaced) */
static bool password_valid(const char *pw)
{
    if (strlen(pw) != DASH_WIFI_PASSWORD_LEN) {
        return false;
    }
    for (int i = 0; i < DASH_WIFI_PASSWORD_LEN; i++) {
        bool dash = i % 4 == 3;
        if (dash ? pw[i] != '-' : !((pw[i] >= 'a' && pw[i] <= 'z') || (pw[i] >= '2' && pw[i] <= '9'))) {
            return false;
        }
    }
    return true;
}

static void new_password(void)
{
    uint8_t rnd[12];
    s.wifi->random(s.wifi->ctx, rnd, sizeof(rnd));
    dash_state_make_password(rnd, s.wifi_password);
    if (s.store != NULL && s.store->set_str != NULL) {
        s.store->set_str(s.store->ctx, DASH_KEY_WIFI_PASSWORD, s.wifi_password);
    }
}

void dash_state_init(const dash_store_t *store, const dash_wifi_t *wifi)
{
    memset(&s, 0, sizeof(s));
    s.store = store;
    s.wifi = wifi;
    s.requests_seen = s_requests;
    if (store != NULL && store->get_str != NULL &&
        (!store->get_str(store->ctx, DASH_KEY_WIFI_PASSWORD, s.wifi_password, sizeof(s.wifi_password)) ||
         !password_valid(s.wifi_password))) {
        s.wifi_password[0] = '\0';   /* generated when the editor is switched on */
    }
    s.cur.display_brightness =
        load(DASH_KEY_DISPLAY_BRIGHTNESS, DASH_MIN_DISPLAY_BRIGHTNESS, 100, DASH_DEF_DISPLAY_BRIGHTNESS);
    s.cur.measure_decimals = load(DASH_KEY_MEASURE_DECIMALS, 0, DASH_MAX_MEASURE_DECIMALS, DASH_DEF_MEASURE_DECIMALS);
    s.cur.measure_add_backward = load(DASH_KEY_MEASURE_ADD_BW, 0, 1, 0);
    s.saved = s.cur;
    s.ride_panel = UI_RIDE_PANEL_LIGHTS;   /* not stored, starts with "Licht" (ADR 0018) */
}

static void save_if_changed(const char *key, uint8_t cur, uint8_t *saved)
{
    if (cur != *saved) {
        s.store->set_u8(s.store->ctx, key, cur);
        *saved = cur;
    }
}

void dash_state_flush(void)
{
    if (!s.dirty) {
        return;
    }
    s.dirty = false;
    if (s.store == NULL) {
        s.saved = s.cur;
        return;
    }
    save_if_changed(DASH_KEY_DISPLAY_BRIGHTNESS, s.cur.display_brightness, &s.saved.display_brightness);
    save_if_changed(DASH_KEY_MEASURE_DECIMALS, s.cur.measure_decimals, &s.saved.measure_decimals);
    save_if_changed(DASH_KEY_MEASURE_ADD_BW, s.cur.measure_add_backward, &s.saved.measure_add_backward);
}

void dash_state_update(uint32_t now_ms, uint32_t total_mm, uint32_t position_mm)
{
    s.now_ms = now_ms;
    s.total_mm = total_mm;
    s.position_mm = position_mm;
    if (s.dirty && now_ms - s.changed_ms >= DASH_SAVE_DELAY_MS) {
        dash_state_flush();
    }
    uint32_t req = s_requests;
    if (req != s.requests_seen) {
        s.requests_seen = req;
        s.editor_activity_ms = now_ms;
    }
    if (s.editor_on && now_ms - s.editor_activity_ms >= DASH_EDITOR_IDLE_MS) {
        s.wifi->stop(s.wifi->ctx);
        s.editor_on = false;
        printf("[dash] editor: Wi-Fi off after 10 min without request\n");
    }
}

static void changed(void)
{
    s.dirty = true;
    s.changed_ms = s.now_ms;   /* every change restarts the delay */
}

/* counter used by the measurements (ADR 0015) */
static uint32_t counter(void)
{
    return s.cur.measure_add_backward ? s.total_mm : s.position_mm;
}

void dash_state_fill_model(ui_model_t *m)
{
    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        m->measure_mm[i] = measurement_value(&s.meas[i], counter(), NULL);
        m->measure_running[i] = s.meas[i].running;
    }
    m->display_brightness = s.cur.display_brightness;
    m->measure_decimals = s.cur.measure_decimals;
    m->measure_add_backward = s.cur.measure_add_backward != 0;
    m->ride_panel = s.ride_panel;

    m->editor_on = s.editor_on;
    snprintf(m->wifi_ssid, sizeof(m->wifi_ssid), "%s", DASH_WIFI_SSID);
    snprintf(m->wifi_password, sizeof(m->wifi_password), "%s", s.wifi_password);
    snprintf(m->editor_url, sizeof(m->editor_url), "%s", DASH_EDITOR_URL);
    m->wifi_clients = s.editor_on ? s.wifi->clients(s.wifi->ctx) : 0;
    uint32_t idle = s.now_ms - s.editor_activity_ms;
    m->editor_remaining_s = 0;
    if (s.editor_on && idle + DASH_EDITOR_WARN_MS > DASH_EDITOR_IDLE_MS && idle < DASH_EDITOR_IDLE_MS) {
        m->editor_remaining_s = (uint16_t)((DASH_EDITOR_IDLE_MS - idle + 999) / 1000);
    }
}

/* ---- GUI actions -------------------------------------------------------- */

static void act_measure_start(void *ctx, uint8_t i)
{
    (void)ctx;
    if (i < UI_MEASURE_COUNT) {
        measurement_start(&s.meas[i], counter());
        printf("[dash] measurement %u start\n", i + 1);
    }
}

static void act_measure_stop(void *ctx, uint8_t i)
{
    (void)ctx;
    if (i < UI_MEASURE_COUNT) {
        measurement_stop(&s.meas[i], counter());
        printf("[dash] measurement %u stop\n", i + 1);
    }
}

static void act_measure_reset(void *ctx, uint8_t i)
{
    (void)ctx;
    if (i < UI_MEASURE_COUNT) {
        measurement_reset(&s.meas[i], counter());
        printf("[dash] measurement %u reset\n", i + 1);
    }
}

static void act_set_measure_add_backward(void *ctx, bool add)
{
    (void)ctx;
    if ((uint8_t)add == s.cur.measure_add_backward) {
        return;
    }
    uint32_t old_counter = counter();
    s.cur.measure_add_backward = add;
    for (int i = 0; i < UI_MEASURE_COUNT; i++) {
        measurement_switch_counter(&s.meas[i], old_counter, counter());
    }
    changed();
    printf("[dash] measurements: backward travel %s\n", add ? "added" : "subtracted");
}

static void act_set_display_brightness(void *ctx, uint8_t percent)
{
    (void)ctx;
    if (percent < DASH_MIN_DISPLAY_BRIGHTNESS) percent = DASH_MIN_DISPLAY_BRIGHTNESS;
    if (percent > 100) percent = 100;
    if (percent != s.cur.display_brightness) {
        s.cur.display_brightness = percent;
        changed();
    }
}

static void act_set_measure_decimals(void *ctx, uint8_t decimals)
{
    (void)ctx;
    if (decimals <= DASH_MAX_MEASURE_DECIMALS && decimals != s.cur.measure_decimals) {
        s.cur.measure_decimals = decimals;
        changed();
        printf("[dash] measurement resolution: %u decimals\n", decimals);
    }
}

static void act_set_ride_panel(void *ctx, uint8_t panel)
{
    (void)ctx;
    s.ride_panel = panel == UI_RIDE_PANEL_MEASURE ? UI_RIDE_PANEL_MEASURE : UI_RIDE_PANEL_LIGHTS;
}

static void act_set_editor(void *ctx, bool on)
{
    (void)ctx;
    if (on == s.editor_on || s.wifi == NULL) {
        return;
    }
    if (on) {
        if (s.wifi_password[0] == '\0') {
            new_password();
        }
        s.editor_activity_ms = s.now_ms;
        s.editor_on = s.wifi->start(s.wifi->ctx, DASH_WIFI_SSID, s.wifi_password);
        printf("[dash] editor: Wi-Fi %s\n", s.editor_on ? "on" : "could not be started");
    } else {
        s.wifi->stop(s.wifi->ctx);
        s.editor_on = false;
        printf("[dash] editor: Wi-Fi off\n");
    }
}

static void act_new_wifi_password(void *ctx)
{
    (void)ctx;
    if (s.wifi == NULL) {
        return;
    }
    new_password();
    printf("[dash] editor: new Wi-Fi password\n");
    if (s.editor_on) {   /* connected devices need the new password */
        s.wifi->stop(s.wifi->ctx);
        s.editor_activity_ms = s.now_ms;
        s.editor_on = s.wifi->start(s.wifi->ctx, DASH_WIFI_SSID, s.wifi_password);
    }
}

void dash_state_actions(ui_actions_t *out, const ui_actions_t *core)
{
    *out = *core;
    out->measure_start = act_measure_start;
    out->measure_stop = act_measure_stop;
    out->measure_reset = act_measure_reset;
    out->set_display_brightness = act_set_display_brightness;
    out->set_measure_decimals = act_set_measure_decimals;
    out->set_measure_add_backward = act_set_measure_add_backward;
    out->set_ride_panel = act_set_ride_panel;
    out->set_editor = act_set_editor;
    out->new_wifi_password = act_new_wifi_password;
}
