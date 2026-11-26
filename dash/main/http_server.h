/*
 * HTTP server of the preset editor (ADR 0013): serves the embedded web editor
 * (web_files.h) and the API (dash/app/editor_api). Every request counts as
 * editor activity (auto-off, dash_state).
 */
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include "editor_api.h"
#include "esp_err.h"

typedef struct {
    const editor_api_t *api;   /* api->store NULL: no presets available (503) */
    void (*lock)(void);        /* shared with the GUI task */
    void (*unlock)(void);
} http_server_config_t;

/* config must stay valid while the server runs */
esp_err_t http_server_start(const http_server_config_t *config);
void http_server_stop(void);

#endif /* HTTP_SERVER_H */
