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
  → output; latency compensated by predicting `offset += v × latency`
- Single source of truth for odometer values, stored in NVS (ADR 0009):
  every 100 m, on stop if ≥ 10 m since the last save, or on command from dash;
  number of writes is bounded by distance, a write counter is kept
- Runs fully without the display
- Chip revision v0.2; PSRAM disabled (faulty on the modules in use, ADR 0007)
- Pin constraints: GPIO 35–37 are wired to the in-package PSRAM (do not use, even
  with PSRAM disabled); inputs are not 5 V tolerant

### dash – ESP32-P4 (Guition JC4880P443)

Display only. 4.3", 480×800, MIPI-DSI, GT911 touch, 32 MB PSRAM, 16 MB flash,
ESP32-C6 as Wi-Fi co-processor (not needed for core functionality).

- Shows speed (smoothed over 200–500 ms), trip and total distance, max speed,
  ride time, battery voltage
- Distance measurement: button starts counting from 0, computed from `total_mm`;
  only the start value is stored in dash NVS (ADR 0008)
- Touch: select pattern, brightness, reset trip
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
| LED pitch (current strip) | 4 mm (250 LEDs/m) |
| LEDs on 0.5 m | 125 |
| Stutter per frame | v / fps |
| Target speed | 4 m/s |
| WS2812B, 125 LEDs | ~245 fps → ~16 mm at 4 m/s |
| SK9822, 125 LEDs, 10 MHz SPI | ~2000+ fps → ~2 mm at 4 m/s |
| Latency at 4 m/s | 1 ms ≙ 4 mm (one pitch) → compensation required |
| Encoder resolution | ≥ 1 count per 4 mm of travel |
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

## Open points

- SK9822 strip: check availability at 4 mm pitch (fallback 144 LEDs/m)
- Measure wheel diameter; choose encoder model (≥ 5000 rpm) and gear ratio
- Power supply (5 V, several A for the strip)
