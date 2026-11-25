/*
 * Flash endurance test for the core board (ESP32-S3).
 *
 * Runs in an endless loop of rounds. Each round:
 *   1. Raw test on the "flashtest" partition (1 MB):
 *      erase -> check erased (0xFF) -> write pattern -> read back and verify.
 *   2. NVS test on the "nvs" partition: writes an odometer-like record
 *      NVS_WRITES_PER_ROUND times (set_blob + commit), reading it back each time.
 *
 * Power-loss test: unplug the board at any time. After the next boot, the test
 * reports the record from the previous run. It must be readable and consistent
 * (NVS keeps either the old or the new value, never a corrupted one).
 *
 * After the test, erase the flash before flashing the core firmware
 * (idf.py erase-flash), so the test data in NVS is removed.
 */
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>

#include "esp_flash.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "flashtest";

#define SECTOR_SIZE          4096
#define NVS_WRITES_PER_ROUND 500
#define NVS_NAMESPACE        "ftest"

/* Similar to the planned odometer record (ADR 0009). */
typedef struct {
    uint32_t trip_m;
    uint32_t total_m;
    uint32_t total_mm;
    uint32_t writes;   /* write counter, incremented on every save */
} odo_record_t;

/* Test statistics, kept across reboots. */
typedef struct {
    uint32_t boots;
    uint32_t rounds;
    uint32_t raw_errors;
    uint32_t nvs_errors;
} stats_t;

static uint32_t s_buf[SECTOR_SIZE / sizeof(uint32_t)];

/* Pattern that differs per word and per pass. */
static inline uint32_t pattern(uint32_t word_index, uint32_t pass)
{
    uint32_t x = word_index * 2654435761u ^ (pass * 0x9E3779B9u + 0x7F4A7C15u);
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    return x;
}

static uint32_t ms_since(int64_t t0)
{
    return (uint32_t)((esp_timer_get_time() - t0) / 1000);
}

/* Returns the number of faulty words (or sectors on API errors). */
static uint32_t raw_pass(const esp_partition_t *part, uint32_t pass)
{
    const size_t words = SECTOR_SIZE / sizeof(uint32_t);
    const size_t sectors = part->size / SECTOR_SIZE;
    uint32_t errors = 0;
    esp_err_t err;

    /* 1. Erase */
    int64_t t0 = esp_timer_get_time();
    err = esp_partition_erase_range(part, 0, part->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "erase failed: %s", esp_err_to_name(err));
        return 1;
    }
    uint32_t t_erase = ms_since(t0);

    /* 2. Check erased state and 3. write pattern */
    t0 = esp_timer_get_time();
    for (size_t s = 0; s < sectors; s++) {
        size_t offset = s * SECTOR_SIZE;

        err = esp_partition_read(part, offset, s_buf, SECTOR_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read (erased) failed at 0x%06x: %s", (unsigned)offset, esp_err_to_name(err));
            errors++;
            continue;
        }
        for (size_t w = 0; w < words; w++) {
            if (s_buf[w] != 0xFFFFFFFFu) {
                if (errors < 5) {
                    ESP_LOGE(TAG, "not erased at 0x%06x: 0x%08" PRIx32,
                             (unsigned)(offset + w * 4), s_buf[w]);
                }
                errors++;
            }
        }

        for (size_t w = 0; w < words; w++) {
            s_buf[w] = pattern((uint32_t)(s * words + w), pass);
        }
        err = esp_partition_write(part, offset, s_buf, SECTOR_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "write failed at 0x%06x: %s", (unsigned)offset, esp_err_to_name(err));
            errors++;
        }
        if ((s & 0x0F) == 0) {
            vTaskDelay(1);   /* let the idle task run (task watchdog) */
        }
    }
    uint32_t t_write = ms_since(t0);

    /* 4. Read back and verify */
    t0 = esp_timer_get_time();
    for (size_t s = 0; s < sectors; s++) {
        size_t offset = s * SECTOR_SIZE;
        err = esp_partition_read(part, offset, s_buf, SECTOR_SIZE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "read failed at 0x%06x: %s", (unsigned)offset, esp_err_to_name(err));
            errors++;
            continue;
        }
        for (size_t w = 0; w < words; w++) {
            uint32_t expected = pattern((uint32_t)(s * words + w), pass);
            if (s_buf[w] != expected) {
                if (errors < 5) {
                    ESP_LOGE(TAG, "mismatch at 0x%06x: 0x%08" PRIx32 " instead of 0x%08" PRIx32,
                             (unsigned)(offset + w * 4), s_buf[w], expected);
                }
                errors++;
            }
        }
        if ((s & 0x0F) == 0) {
            vTaskDelay(1);
        }
    }
    uint32_t t_verify = ms_since(t0);

    ESP_LOGI(TAG, "  raw: erase %" PRIu32 " ms, check+write %" PRIu32 " ms, verify %" PRIu32 " ms, errors %" PRIu32,
             t_erase, t_write, t_verify, errors);
    return errors;
}

/* Returns the number of failed NVS writes/reads. */
static uint32_t nvs_round(nvs_handle_t h, odo_record_t *rec)
{
    uint32_t errors = 0;
    int64_t t0 = esp_timer_get_time();

    for (int i = 0; i < NVS_WRITES_PER_ROUND; i++) {
        rec->writes++;
        rec->trip_m += 10;
        rec->total_m += 10;
        rec->total_mm += 10000;

        esp_err_t err = nvs_set_blob(h, "rec", rec, sizeof(*rec));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        if (err != ESP_OK) {
            if (errors < 5) {
                ESP_LOGE(TAG, "nvs write failed: %s", esp_err_to_name(err));
            }
            errors++;
            continue;
        }

        odo_record_t check;
        size_t len = sizeof(check);
        err = nvs_get_blob(h, "rec", &check, &len);
        if (err != ESP_OK || len != sizeof(check) || memcmp(&check, rec, sizeof(check)) != 0) {
            if (errors < 5) {
                ESP_LOGE(TAG, "nvs read-back mismatch (%s)", esp_err_to_name(err));
            }
            errors++;
        }
    }

    nvs_stats_t st;
    nvs_get_stats("nvs", &st);
    uint32_t t = ms_since(t0);
    ESP_LOGI(TAG, "  nvs: %d writes in %" PRIu32 " ms (%" PRIu32 " ms/write), errors %" PRIu32
             ", entries used %u / free %u",
             NVS_WRITES_PER_ROUND, t, t / NVS_WRITES_PER_ROUND, errors,
             (unsigned)st.used_entries, (unsigned)st.free_entries);
    return errors;
}

void app_main(void)
{
    uint32_t flash_id = 0;
    esp_flash_read_id(NULL, &flash_id);
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    ESP_LOGI(TAG, "Flash endurance test – flash ID 0x%06" PRIx32 " (manufacturer 0x%02" PRIx32 "), %" PRIu32 " MB",
             flash_id, flash_id >> 16, flash_size / (1024 * 1024));

    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_UNDEFINED, "flashtest");
    if (part == NULL) {
        ESP_LOGE(TAG, "partition 'flashtest' not found – check partitions.csv");
        return;
    }
    ESP_LOGI(TAG, "flashtest partition: offset 0x%06" PRIx32 ", size %" PRIu32 " KB",
             part->address, part->size / 1024);

    /* NVS init. Do not erase silently on errors: that would hide corruption. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS has a newer format version – erasing");
        nvs_flash_erase();
        err = nvs_flash_init();
    } else if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
        ESP_LOGE(TAG, "NVS reports no free pages – possible corruption! Erasing to continue.");
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h));

    /* Results from the previous run (power-loss test) */
    stats_t stats = {0};
    size_t len = sizeof(stats);
    if (nvs_get_blob(h, "stats", &stats, &len) != ESP_OK || len != sizeof(stats)) {
        memset(&stats, 0, sizeof(stats));
    }
    stats.boots++;

    odo_record_t rec = {0};
    len = sizeof(rec);
    err = nvs_get_blob(h, "rec", &rec, &len);
    if (err == ESP_OK && len == sizeof(rec)) {
        bool consistent = (rec.total_mm == rec.total_m * 1000u) && (rec.trip_m == rec.total_m);
        ESP_LOGI(TAG, "previous record: writes %" PRIu32 ", total %" PRIu32 " m – %s",
                 rec.writes, rec.total_m, consistent ? "consistent" : "INCONSISTENT!");
        if (!consistent) {
            stats.nvs_errors++;
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "no previous record (first run)");
        memset(&rec, 0, sizeof(rec));
    } else {
        ESP_LOGE(TAG, "previous record unreadable: %s", esp_err_to_name(err));
        stats.nvs_errors++;
        memset(&rec, 0, sizeof(rec));
    }
    ESP_LOGI(TAG, "boot %" PRIu32 ", rounds so far %" PRIu32 ", raw errors %" PRIu32 ", nvs errors %" PRIu32,
             stats.boots, stats.rounds, stats.raw_errors, stats.nvs_errors);

    while (true) {
        uint32_t round = stats.rounds + 1;
        ESP_LOGI(TAG, "round %" PRIu32 ":", round);

        stats.raw_errors += raw_pass(part, round);
        stats.nvs_errors += nvs_round(h, &rec);
        stats.rounds = round;

        if (nvs_set_blob(h, "stats", &stats, sizeof(stats)) == ESP_OK) {
            nvs_commit(h);
        }

        ESP_LOGI(TAG, "=> total: %" PRIu32 " rounds, %" PRIu32 " NVS writes, raw errors %" PRIu32
                 ", nvs errors %" PRIu32 " – %s",
                 stats.rounds, rec.writes, stats.raw_errors, stats.nvs_errors,
                 (stats.raw_errors == 0 && stats.nvs_errors == 0) ? "OK" : "ERRORS");
    }
}
