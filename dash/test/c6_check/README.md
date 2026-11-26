# C6 check and update (ADR 0013, 0019)

Test program for the P4. It uses the dash partition table. It reads the ESP-Hosted firmware version of the
ESP32-C6 co-processor over SDIO and does one Wi-Fi scan through it (only the
number of networks is logged). The NVS partition of the dash is not erased.
Afterwards flash the dash firmware again (`cd dash && idf.py flash`).

    cd dash/test/c6_check
    idf.py set-target esp32p4 build flash monitor

Versions (keep host and C6 equal): `espressif/esp_hosted` ~2.13.1 and
`espressif/esp_wifi_remote` 1.6.3 (the last version that excludes ESP-Hosted
3.x; newer ones let the component manager pick 3.x and fail in kconfig).

## Updating the C6 (OTA over SDIO)

1. Build the C6 firmware from the same ESP-Hosted version: copy
   `managed_components/espressif__esp_hosted/slave` and `.../common` side by side
   into a scratch folder, then in `slave/`:
   `idf.py set-target esp32c6 build` → `build/network_adapter.bin`.
2. Write it into the partition `c6fw` of the P4 (`dash/partitions.csv`):
   `parttool.py -p <port> --partition-table-file ../../partitions.csv write_partition --partition-name c6fw --input network_adapter.bin`
   (`parttool.py` is in `$IDF_PATH/components/partition_table/`)
3. Enable `C6_CHECK_OTA` in menuconfig ("C6 check"), build, flash, monitor.
   The program checks magic and chip id of the image, skips the update if the
   C6 already runs that version, sends the image (~12 s), and restarts.
   If the transfer or the image check on the C6 fails, the C6 keeps its old
   firmware. Last resort: flash the C6 over its UART0 pins.

Note: on the P4 the main task stack can lie in TCM, where flash operations
assert (`esp_task_stack_is_sane_cache_disabled`); the work therefore runs in a
task with a stack in internal RAM.

## Results

- 2026-11-26: factory firmware ESP-Hosted 2.3.2 (not in the component
  registry). Hosts 3.1.0 and 2.13.1 connected and scanned, but warned about
  the version gap ("Upgrade co-proc to avoid RPC timeouts").
- 2026-11-26: C6 updated by OTA to 2.13.1 in 11.5 s (image then still at the raw
  offset 0x800000, before the dash partition table existed); afterwards no version
  warning, transport size reported (1536), Wi-Fi scan works.
