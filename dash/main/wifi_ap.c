/* Wi-Fi access point of the preset editor, see wifi_ap.h. */
#include "wifi_ap.h"

#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_hosted.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "wifi_ap";

#define CHANNEL     6
#define MAX_CLIENTS 2

typedef struct {
    bool start;
    char ssid[33];
    char password[65];
} cmd_t;

static QueueHandle_t s_queue;
static const http_server_config_t *s_http;
static volatile uint8_t s_clients;
static bool s_wifi_ready;   /* esp_wifi_init done (worker task only) */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        s_clients++;
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED && s_clients > 0) {
        s_clients--;
    }
}

static bool wifi_init_once(void)
{
    if (s_wifi_ready) {
        return true;
    }
    esp_netif_create_default_wifi_ap();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init: %s", esp_err_to_name(err));
        return false;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM);   /* the configuration is set at every start */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    s_wifi_ready = true;
    return true;
}

static void do_start(const cmd_t *c)
{
    if (!wifi_init_once()) {
        return;
    }
    wifi_config_t cfg = {0};
    memcpy(cfg.ap.ssid, c->ssid, strlen(c->ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(c->ssid);
    memcpy(cfg.ap.password, c->password, strlen(c->password));
    cfg.ap.channel = CHANNEL;
    cfg.ap.max_connection = MAX_CLIENTS;
    cfg.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;   /* WPA3 for new devices, WPA2 for older ones */
    cfg.ap.pmf_cfg.capable = true;
    cfg.ap.pmf_cfg.required = false;
    cfg.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "WPA2/WPA3 not accepted (%s), using WPA2", esp_err_to_name(err));
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
        err = esp_wifi_set_config(WIFI_IF_AP, &cfg);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "access point not started: %s", esp_err_to_name(err));
        return;
    }
    s_clients = 0;
    http_server_start(s_http);
    ESP_LOGI(TAG, "access point %s on channel %d", c->ssid, CHANNEL);
}

static void do_stop(void)
{
    http_server_stop();
    if (s_wifi_ready) {
        esp_wifi_stop();
    }
    s_clients = 0;
    ESP_LOGI(TAG, "access point off");
}

static void worker(void *arg)
{
    (void)arg;
    /* bring up the C6 link now, so switching on later is quick */
    ESP_LOGI(TAG, "before C6 link: internal free %u KB (DMA %u KB, largest %u KB)",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL) / 1024));
    int rc = esp_hosted_connect_to_slave();
    ESP_LOGI(TAG, "C6 link: %s", rc == 0 ? "ready" : "failed");
    cmd_t c;
    while (xQueueReceive(s_queue, &c, portMAX_DELAY) == pdTRUE) {
        if (c.start) {
            do_start(&c);
        } else {
            do_stop();
        }
    }
}

void wifi_ap_init(const http_server_config_t *http)
{
    s_http = http;
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    s_queue = xQueueCreate(4, sizeof(cmd_t));
    /* stack in internal RAM: flash and Wi-Fi calls need it */
    xTaskCreateWithCaps(worker, "wifi_ap", 6144, NULL, 4, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

/* ---- dash_wifi_t --------------------------------------------------------------- */

static bool ap_start(void *ctx, const char *ssid, const char *password)
{
    (void)ctx;
    cmd_t c = {.start = true};
    snprintf(c.ssid, sizeof(c.ssid), "%s", ssid);
    snprintf(c.password, sizeof(c.password), "%s", password);
    return s_queue != NULL && xQueueSend(s_queue, &c, 0) == pdTRUE;
}

static void ap_stop(void *ctx)
{
    (void)ctx;
    cmd_t c = {.start = false};
    if (s_queue != NULL) {
        xQueueSend(s_queue, &c, pdMS_TO_TICKS(100));
    }
}

static uint8_t ap_clients(void *ctx)
{
    (void)ctx;
    return s_clients;
}

/* The P4 has no radio: enable the hardware entropy source while generating
   (the C6 does Wi-Fi, so it is free here) */
static void ap_random(void *ctx, uint8_t *buf, size_t len)
{
    (void)ctx;
    bootloader_random_enable();
    esp_fill_random(buf, len);
    bootloader_random_disable();
}

const dash_wifi_t wifi_ap = {
    .ctx = NULL,
    .start = ap_start,
    .stop = ap_stop,
    .clients = ap_clients,
    .random = ap_random,
};
