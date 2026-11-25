# unmoving_pixel – Architecture

## Purpose

An LED strip (~0.5 m) is mounted lengthwise on a kick scooter. The pattern on the strip
is shifted by the distance travelled, so it appears fixed to the ground while the
scooter moves ("unmoving plaid" effect). A rotary encoder on the rear wheel measures
the distance. A dashboard display on the handlebar shows speed and distance.

Target speed: up to 4 m/s.

## System overview

```
            ~1 m cable (deck → handlebar)
 ┌──────────────────────┐   UART / CRC16   ┌──────────────────────┐
 │ core  (ESP32-S3)     │ ───────────────► │ dash  (ESP32-P4)     │
 │                      │ ◄─────────────── │ JC4880P443, 4.3"     │
 │ - encoder (PCNT)     │   commands/ACK   │ - speed, distance    │
 │ - LED output (SPI)   │                  │ - touch controls     │
 │ - odometer (NVS)     │                  │                      │
 └───┬─────────────┬────┘                  └──────────────────────┘
     │ few cm      │ few cm
 ┌───┴─────┐  ┌────┴──────────┐
 │ encoder │  │ LED strip     │
 │ (rear   │  │ ~0.5 m        │
 │  wheel) │  │               │
 └─────────┘  └───────────────┘
```

## Components

### core – ESP32-S3 (N16R8)

Real-time part. Mounted next to the encoder and the LED strip with short wires
(low capacitive load, fewer EMC issues).

- Encoder via the hardware pulse counter (PCNT): quadrature x4, glitch filter
  100 ns–1 µs, `accum_count` enabled (counter is 16-bit)
- LED output: SK9822 via hardware SPI with DMA (ADR 0003)
- Render loop: position from encoder → pattern offset → sub-pixel rendering
  → output; latency kept short (< 1 ms) and constant (ADR 0003); optional
  compensation `offset += v × latency` with smoothed speed
- Patterns (ground-fixed): repeating lists of segments with length (cm), colour
  and brightness, hard edges; up to 2048 segments; built-in and up to 32 user
  slots plus a RAM preview slot; rendered with a box filter over each LED
  footprint (ADR 0011)
- Overlay (strip-fixed, moves with the scooter): edge fade with two S-curves,
  levels at start/middle/end and fade lengths; factor 0…1 per LED (ADR 0012)
- Brightness per LED: pattern × overlay × global, split into SK9822 5-bit
  current and 8-bit PWM, current limit (ADR 0012)
- Presets (patterns, overlays) and LED settings stored in a separate NVS
  partition `cfg`; built-in presets compiled from `presets/` in the repository;
  settings saved 2 s after the last change (ADR 0011–0013)
- Single source of truth for odometer values, stored in NVS (ADR 0009):
  every 100 m, on stop if ≥ 10 m since the last save, or on command from dash;
  number of writes is bounded by distance, a write counter is kept
- Runs fully without the display
- Chip revision v0.2; PSRAM disabled (faulty on the modules in use, ADR 0007)
- Pin constraints: GPIO 35–37 are wired to the in-package PSRAM (do not use, even
  with PSRAM disabled); inputs are not 5 V tolerant

### dash – ESP32-P4 (Guition JC4880P443)

Display and user interface. 4.3", 480×800, MIPI-DSI, GT911 touch, 32 MB PSRAM,
16 MB flash, ESP32-C6 as Wi-Fi co-processor (only used on demand for the pattern
editor, ADR 0013; not needed for riding).

- Shows speed (smoothed over 200–500 ms), trip and total distance, max speed,
  ride time, battery voltage
- Distance measurement: stopwatch with start/stop/reset, computed from
  `total_mm`; only the measurement state is stored in dash NVS (ADR 0008)
- GUI written in C with LVGL 9, previewed in a PC simulator on macOS
  (`dash/ui`, `dash/main`, `dash/sim`, ADR 0010). Two screens:
  - Ride: speed, trip/total distance, distance measurement (start/stop/reset)
  - LED: global brightness, pattern list, overlay list and overlay sliders,
    Wi-Fi editor on/off
- Preset editor: on demand, dash opens a Wi-Fi access point and serves a web
  editor for patterns and overlays; presets are validated and forwarded to core
  over UART (ADR 0013)
- Shows "--" if no valid frame was received for 500 ms
- UART to core: UART1 on GPIO33 = RX, GPIO31 = TX (ADR 0006)
- Chip revision v1.3 (confirmed on the board): support for revisions < 3.0 is
  enabled in `sdkconfig.defaults`
- Flash chip is made by Boya: `CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP=y`
- PSRAM (32 MB, hex mode) runs at 200 MHz

### common

Shared protocol definition (frame layout, message types, CRC16) used by both
firmwares.

## Key constraints and figures

| Item | Value |
|---|---|
| LED strip | SK9822, 144 LEDs/m, pitch ~6.94 mm |
| LEDs on 0.5 m | 72 |
| Stutter per frame | v / fps |
| Target speed | 4 m/s |
| WS2812B, 125 LEDs (4 mm, for comparison) | ~245 fps → ~16 mm at 4 m/s |
| SK9822, 72 LEDs, 10 MHz SPI | ~4000 fps → ~1 mm at 4 m/s |
| Latency at 4 m/s | 1 ms ≙ 4 mm shift; constant shift is invisible, jitter is visible → keep latency constant |
| Encoder resolution | ≥ 1 count per 4 mm of travel (finer than the LED pitch) |
| Encoder shaft at 4 m/s (10:1, ~200 mm wheel) | ~3800 rpm |

## Build

- ESP-IDF ≥ 5.3 (5.4/5.5 recommended)
- `idf.py set-target esp32s3` (core) / `idf.py set-target esp32p4` (dash)
- `sdkconfig` is not committed; `sdkconfig.defaults` and `dependencies.lock` are

## Decisions

See [`adr/`](adr/).

| ADR | Title | Status |
|---|---|---|
| [0001](adr/0001-split-realtime-and-display-controllers.md) | Split real-time and display controllers | Accepted |
| [0002](adr/0002-custom-firmware-instead-of-wled.md) | Custom firmware instead of WLED | Accepted |
| [0003](adr/0003-clocked-leds-sk9822.md) | Clocked LEDs (SK9822) | Accepted |
| [0004](adr/0004-uart-protocol-core-dash.md) | UART protocol between core and dash | Accepted |
| [0005](adr/0005-friction-wheel-encoder.md) | Friction wheel encoder on the rear tyre | Proposed |
| [0006](adr/0006-dash-uart-pins.md) | UART pins on dash (JC4880P443) | Accepted |
| [0007](adr/0007-core-without-psram.md) | core runs without PSRAM | Accepted |
| [0008](adr/0008-distance-measurement-on-dash.md) | Distance measurement on dash | Accepted |
| [0009](adr/0009-odometer-persistence-policy.md) | Odometer persistence policy on core | Accepted |
| [0010](adr/0010-dash-gui-in-code-with-pc-simulator.md) | dash GUI defined in code, with a PC simulator | Accepted |
| [0011](adr/0011-led-pattern-model.md) | LED pattern model | Accepted |
| [0012](adr/0012-led-brightness-and-strip-overlay.md) | LED brightness and strip overlay | Accepted |
| [0013](adr/0013-preset-editor-via-wifi-and-repository-presets.md) | Preset editor via Wi-Fi on dash, presets in the repository | Accepted |

## Open points

- Measure wheel diameter; choose encoder model (≥ 5000 rpm) and gear ratio
- Power supply (5 V, several A for the strip); sets the LED current limit (ADR 0012)
- core partition table: add NVS partition `cfg` (512 KB) for settings and presets
- dash partition table: NVS, app, web editor files (ADR 0013)
- ESP32-C6: check ESP-Hosted firmware version, update if needed (ADR 0013)
