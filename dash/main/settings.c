/* dash NVS storage, see settings.h. */
#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";
static nvs_handle_t s_nvs;

static bool get_u8(void *ctx, const char *key, uint8_t *value)
{
    (void)ctx;
    return nvs_get_u8(s_nvs, key, value) == ESP_OK;
}

static void set_u8(void *ctx, const char *key, uint8_t value)
{
    (void)ctx;
    esp_err_t err = nvs_set_u8(s_nvs, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "saved %s = %u", key, value);
    } else {
        ESP_LOGW(TAG, "saving %s failed: %s", key, esp_err_to_name(err));
    }
}

static const dash_store_t k_store = {NULL, get_u8, set_u8};

const dash_store_t *settings_store(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS erased (%s), settings back to defaults", esp_err_to_name(err));
        if (nvs_flash_erase() == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err == ESP_OK) {
        err = nvs_open("dash", NVS_READWRITE, &s_nvs);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS not usable (%s): defaults, nothing saved", esp_err_to_name(err));
        return NULL;
    }
    return &k_store;
}
