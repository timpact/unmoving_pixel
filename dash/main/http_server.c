/* HTTP server of the preset editor, see http_server.h. */
#include "http_server.h"

#include <string.h>

#include "cJSON.h"
#include "dash_state.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "web_files.h"

static const char *TAG = "http";
static httpd_handle_t s_server;
static const http_server_config_t *s_cfg;

static const char *status_text(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 304: return "304 Not Modified";
    case 400: return "400 Bad Request";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 409: return "409 Conflict";
    case 413: return "413 Payload Too Large";
    case 500: return "500 Internal Server Error";
    case 502: return "502 Bad Gateway";
    case 503: return "503 Service Unavailable";
    case 507: return "507 Insufficient Storage";
    default:  return "500 Internal Server Error";
    }
}

static esp_err_t send_json(httpd_req_t *req, int status, const char *json)
{
    httpd_resp_set_status(req, status_text(status));
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json != NULL ? json : "{}", HTTPD_RESP_USE_STRLEN);
}

/* ---- editor files ------------------------------------------------------------ */

static esp_err_t file_handler(httpd_req_t *req)
{
    dash_state_editor_activity();
    char path[48];
    size_t len = strcspn(req->uri, "?");
    if (len >= sizeof(path)) {
        len = sizeof(path) - 1;
    }
    memcpy(path, req->uri, len);
    path[len] = '\0';
    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }
    for (int i = 0; i < web_file_count; i++) {
        const web_file_t *f = &web_files[i];
        if (strcmp(path, f->path) != 0) {
            continue;
        }
        char inm[24];
        httpd_resp_set_hdr(req, "ETag", f->etag);
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");   /* revalidate with the ETag */
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", inm, sizeof(inm)) == ESP_OK &&
            strcmp(inm, f->etag) == 0) {
            httpd_resp_set_status(req, status_text(304));
            return httpd_resp_send(req, NULL, 0);
        }
        httpd_resp_set_type(req, f->mime);
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)f->data, (ssize_t)f->size);
    }
    httpd_resp_set_status(req, status_text(404));
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    return httpd_resp_send(req, "Nicht gefunden", HTTPD_RESP_USE_STRLEN);
}

/* ---- API ------------------------------------------------------------------- */

static esp_err_t api_handler(httpd_req_t *req)
{
    dash_state_editor_activity();
    if (s_cfg->api->store == NULL) {
        return send_json(req, 503, "{\"error\":\"Keine Verbindung zum core\"}");
    }
    size_t len = req->content_len;
    if (len > EDITOR_API_MAX_BODY) {
        send_json(req, 413, "{\"error\":\"Anfrage zu groß\"}");
        return ESP_FAIL;   /* closes the connection, the body is not read */
    }
    char *body = NULL;
    if (len > 0) {
        body = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (body == NULL) {
            send_json(req, 500, "{\"error\":\"Kein Speicher\"}");
            return ESP_FAIL;
        }
        size_t got = 0;
        while (got < len) {
            int n = httpd_req_recv(req, body + got, len - got);
            if (n == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            if (n <= 0) {
                free(body);
                return ESP_FAIL;
            }
            got += (size_t)n;
        }
        body[len] = '\0';
    }
    static const char *const methods[] = {[HTTP_GET] = "GET", [HTTP_PUT] = "PUT", [HTTP_POST] = "POST",
                                          [HTTP_DELETE] = "DELETE"};
    const char *method = req->method < (int)(sizeof(methods) / sizeof(methods[0])) && methods[req->method] != NULL
                             ? methods[req->method]
                             : "OTHER";
    editor_response_t r;
    s_cfg->lock();
    editor_api_handle(s_cfg->api, method, req->uri, body, len, &r);
    s_cfg->unlock();
    free(body);
    esp_err_t err = send_json(req, r.status, r.body);
    editor_api_free(&r);
    return err;
}

/* cJSON nodes into PSRAM: a large pattern creates thousands of small nodes */
static void *json_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

esp_err_t http_server_start(const http_server_config_t *config)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    s_cfg = config;
    cJSON_Hooks hooks = {json_malloc, free};
    cJSON_InitHooks(&hooks);

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.uri_match_fn = httpd_uri_match_wildcard;
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    esp_err_t err = httpd_start(&s_server, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }
    static const httpd_method_t api_methods[] = {HTTP_GET, HTTP_PUT, HTTP_POST, HTTP_DELETE};
    for (size_t i = 0; i < sizeof(api_methods) / sizeof(api_methods[0]); i++) {
        httpd_uri_t u = {.uri = "/api/*", .method = api_methods[i], .handler = api_handler};
        httpd_register_uri_handler(s_server, &u);
    }
    httpd_uri_t files = {.uri = "/*", .method = HTTP_GET, .handler = file_handler};
    httpd_register_uri_handler(s_server, &files);
    ESP_LOGI(TAG, "editor at http://192.168.4.1 (%d files)", web_file_count);
    return ESP_OK;
}

void http_server_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
    }
}
