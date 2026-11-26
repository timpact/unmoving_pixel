/*
 * C6 check (ADR 0013, 0019): reads the ESP-Hosted firmware of the ESP32-C6
 * co-processor over SDIO and does one Wi-Fi scan through it.
 * Only the number of networks is logged.
 *
 * C6_CHECK_OTA (menuconfig): additionally updates the C6 from the image in the
 * partition c6fw (see README.md). Otherwise the C6 is not changed.
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_app_format.h"
#include "esp_event.h"
#include "esp_partition.h"
#include "esp_hosted.h"
#include "esp_hosted_host_fw_ver.h"
#include "esp_hosted_misc.h"
#include "esp_hosted_ota.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

static const char *TAG = "c6_check";

#if CONFIG_C6_CHECK_OTA
static const esp_partition_t *s_part;   /* "c6fw" (dash/partitions.csv) */
#define OTA_CHUNK  1480

/* Size of the ESP image in the c6fw partition: header, segments, padding and
 * checksum to 16 bytes, SHA-256 if appended. Checks magic and chip (ESP32-C6). */
static size_t image_size(char *version, size_t version_len)
{
    esp_image_header_t h;
    s_part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "c6fw");
    if (s_part == NULL) {
        ESP_LOGE(TAG, "OTA: no partition c6fw (dash/partitions.csv)");
        return 0;
    }
    if (esp_partition_read(s_part, 0, &h, sizeof(h)) != ESP_OK || h.magic != ESP_IMAGE_HEADER_MAGIC) {
        ESP_LOGE(TAG, "OTA: no ESP image in partition c6fw");
        return 0;
    }
    if (h.chip_id != ESP_CHIP_ID_ESP32C6) {
        ESP_LOGE(TAG, "OTA: image is for chip id %u, not the ESP32-C6", h.chip_id);
        return 0;
    }
    size_t off = sizeof(h);
    for (int i = 0; i < h.segment_count; i++) {
        esp_image_segment_header_t seg;
        if (esp_partition_read(s_part, off, &seg, sizeof(seg)) != ESP_OK || seg.data_len > 0x400000) {
            ESP_LOGE(TAG, "OTA: segment %d unreadable", i);
            return 0;
        }
        if (i == 0) {
            esp_app_desc_t d;
            esp_partition_read(s_part, off + sizeof(seg), &d, sizeof(d));
            snprintf(version, version_len, "%.31s", d.version);
            ESP_LOGI(TAG, "OTA: image \"%.31s\" version %s", d.project_name, version);
        }
        off += sizeof(seg) + seg.data_len;
        if (off > s_part->size) {
            ESP_LOGE(TAG, "OTA: image larger than the partition");
            return 0;
        }
    }
    off = (off + 1 + 15) & ~(size_t)15;   /* padding + checksum byte */
    if (h.hash_appended) {
        off += 32;
    }
    return off;
}

static void c6_ota(const esp_hosted_coprocessor_fwver_t *cur)
{
    char version[32] = "";
    size_t size = image_size(version, sizeof(version));
    if (size == 0) {
        return;
    }
    char cur_str[32];
    snprintf(cur_str, sizeof(cur_str), "%" PRIu32 ".%" PRIu32 ".%" PRIu32, cur->major1, cur->minor1, cur->patch1);
    if (strcmp(cur_str, version) == 0) {
        ESP_LOGI(TAG, "OTA: C6 already runs %s, nothing to do", version);
        return;
    }
    ESP_LOGW(TAG, "OTA: updating the C6 from %s to %s (%u bytes) - do not switch off", cur_str, version, (unsigned)size);
    esp_err_t err = esp_hosted_slave_ota_begin();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: begin failed: %s", esp_err_to_name(err));
        return;
    }
    static uint8_t chunk[OTA_CHUNK];
    int64_t t0 = esp_timer_get_time();
    for (size_t off = 0; off < size; off += OTA_CHUNK) {
        size_t n = size - off < OTA_CHUNK ? size - off : OTA_CHUNK;
        err = esp_partition_read(s_part, off, chunk, n);
        if (err == ESP_OK) {
            err = esp_hosted_slave_ota_write(chunk, n);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA: write at %u failed: %s (C6 keeps its old firmware)", (unsigned)off, esp_err_to_name(err));
            esp_hosted_slave_ota_end();
            return;
        }
        if ((off / OTA_CHUNK) % 100 == 0) {
            ESP_LOGI(TAG, "OTA: %u %%", (unsigned)(off * 100 / size));
        }
    }
    err = esp_hosted_slave_ota_end();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA: end (image check on the C6) failed: %s (C6 keeps its old firmware)", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "OTA: transferred in %.1f s", (esp_timer_get_time() - t0) / 1e6);
    if (cur->major1 > 2 || (cur->major1 == 2 && cur->minor1 >= 6)) {   /* activate exists from 2.6.0 */
        err = esp_hosted_slave_ota_activate();
        ESP_LOGI(TAG, "OTA: activate: %s", esp_err_to_name(err));
    }
    ESP_LOGW(TAG, "OTA: done, restarting the P4 in 3 s (resets the C6 too)");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_restart();
}
#endif

static void check_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "host: ESP-Hosted %d.%d.%d, ESP-IDF %s", ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1,
             ESP_HOSTED_VERSION_PATCH_1, esp_get_idf_version());

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS not readable (%s), not erased: Wi-Fi without NVS", esp_err_to_name(err));
    }
    ESP_ERROR_CHECK(esp_netif_init());
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }

    int rc = esp_hosted_connect_to_slave();
    ESP_LOGI(TAG, "connect to C6: %d", rc);

    esp_hosted_coprocessor_fwver_t fw = {0};
    rc = esp_hosted_get_coprocessor_fwversion(&fw);
    if (rc == 0) {
        ESP_LOGI(TAG, "C6: ESP-Hosted %" PRIu32 ".%" PRIu32 ".%" PRIu32, fw.major1, fw.minor1, fw.patch1);
    } else {
        ESP_LOGE(TAG, "C6 firmware version not readable: %d", rc);
    }
#if CONFIG_C6_CHECK_OTA
    if (rc == 0) {
        c6_ota(&fw);
    }
#endif

    esp_hosted_app_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    if (esp_hosted_get_coprocessor_app_desc(&desc) == ESP_OK) {
        ESP_LOGI(TAG, "C6 app: \"%.32s\" version \"%.32s\", ESP-IDF %.32s, built %.16s %.16s", desc.project_name,
                 desc.version, desc.idf_ver, desc.date, desc.time);
    } else {
        ESP_LOGW(TAG, "C6 app description not readable");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err == ESP_OK) err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err == ESP_OK) err = esp_wifi_start();
    if (err == ESP_OK) err = esp_wifi_scan_start(NULL, true);
    uint16_t n = 0;
    if (err == ESP_OK) err = esp_wifi_scan_get_ap_num(&n);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi scan through the C6: %u networks", n);
        esp_wifi_clear_ap_list();
    } else {
        ESP_LOGE(TAG, "Wi-Fi through the C6 failed: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "done");
    vTaskDelete(NULL);
}

void app_main(void)
{
    /* Flash access needs a stack in internal RAM; the main task stack may end up
       in the TCM of the P4, which flash operations do not accept. */
    xTaskCreateWithCaps(check_task, "c6_check", 8192, NULL, 5, NULL, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
}
