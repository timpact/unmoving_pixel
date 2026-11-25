/*
 * core – minimal bring-up firmware for the ESP32-S3 (N16R8).
 *
 * Prints chip, flash and PSRAM info, then a heartbeat once per second.
 * Used to verify the toolchain, flashing and the board before encoder/LED work.
 */
#include <inttypes.h>
#include <stdio.h>

#include "sdkconfig.h"

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "core";

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "Hello from core (ESP32-S3)");
    ESP_LOGI(TAG, "Chip revision: v%d.%d, cores: %d",
             chip.revision / 100, chip.revision % 100, chip.cores);
    ESP_LOGI(TAG, "Flash: %" PRIu32 " MB", flash_size / (1024 * 1024));
#if CONFIG_SPIRAM
    ESP_LOGI(TAG, "PSRAM: %u MB", (unsigned)(esp_psram_get_size() / (1024 * 1024)));
#else
    ESP_LOGI(TAG, "PSRAM: disabled");
#endif

    uint32_t n = 0;
    while (true) {
        ESP_LOGI(TAG, "alive %" PRIu32, n++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
