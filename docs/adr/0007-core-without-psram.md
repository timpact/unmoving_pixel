# ADR 0007: core runs without PSRAM

- Status: Accepted
- Date: 2026-11-24

## Context

The core board is a low-cost ESP32-S3 N16R8 module (chip revision v0.2).
According to its eFuses it has 8 MB of in-package octal PSRAM (AP Memory, 3.3 V)
and 16 MB of external quad flash. The PSRAM was configured to match
(`CONFIG_SPIRAM_MODE_OCT`).

Observed behaviour with PSRAM enabled (ESP-IDF v5.5.2):

- The PSRAM ID is read correctly ("Found 8MB PSRAM device").
- With the PSRAM memory test enabled, startup hangs right after
  `cpu_start: Multicore app`. The bootloader watchdog resets the chip after 9 s,
  in an endless loop.
- With the memory test disabled, the app crashes with `IllegalInstruction` while
  adding PSRAM to the heap (first write to PSRAM). Code fetched from flash reads as
  `0xFFFFFFFF` at that point, i.e. the shared memory bus is corrupted.
- Same result at 40 MHz and at 80 MHz (with MSPI timing tuning).
- Same result on a second board of the same batch.
- An Arduino sketch with "OPI PSRAM" enabled produced no output either.

With PSRAM disabled, the firmware runs normally.

The most likely cause is faulty PSRAM in this batch of modules.

## Decision

core runs with PSRAM disabled (`CONFIG_SPIRAM` not set in `sdkconfig.defaults`).

## Consequences

- core only has the internal SRAM (~512 KB). This is far more than needed: the
  frame buffer for 72 SK9822 LEDs is ~300 bytes, odometer data a few bytes.
- No PSRAM-related startup risk; faster startup (no memory test).
- GPIO 35, 36 and 37 remain unusable: they are still wired to the in-package PSRAM.
- If a later feature needs large buffers, use a module with working PSRAM
  (e.g. a genuine ESP32-S3-WROOM-1-N16R8) and re-enable it.
