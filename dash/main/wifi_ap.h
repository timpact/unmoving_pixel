/*
 * Wi-Fi access point of the preset editor over the ESP32-C6 (ADR 0013, 0019):
 * ESP-Hosted over SDIO, esp_wifi_remote, WPA2/WPA3, channel 6, max. 2 clients,
 * 192.168.4.1; starts the HTTP server together with the access point.
 *
 * Start and stop run in a worker task, so the GUI never waits for the C6;
 * ESP-Hosted is brought up by that task at dash start (Wi-Fi stays off).
 */
#ifndef WIFI_AP_H
#define WIFI_AP_H

#include "dash_state.h"
#include "http_server.h"

/* Call once after NVS is initialised; http must stay valid */
void wifi_ap_init(const http_server_config_t *http);

/* start, stop, clients, random for dash_state */
extern const dash_wifi_t wifi_ap;

#endif /* WIFI_AP_H */
