# ADR 0001: Split real-time and display controllers

- Status: Accepted
- Date: 2026-11-24

## Context

The system has two very different jobs:

1. Reading the encoder and driving the LED strip with low and constant latency
   (several hundred to over a thousand frames per second).
2. Rendering a touch dashboard with LVGL on a 480×800 MIPI-DSI display.

The ESP32-P4 board (JC4880P443) could do both, but GUI rendering can cause jitter in
the LED loop. Most GPIOs on the display board are already in use (display, touch,
audio, camera, SD card). The display sits on the handlebar, while the encoder and
strip are on the deck and rear wheel, about 1 m away.

## Decision

Use two controllers:

- **core (ESP32-S3)** handles the encoder, LED output and odometer storage. It is
  mounted right next to the encoder and the strip.
- **dash (ESP32-P4)** only displays data and sends user commands.

They are connected by a UART link (see ADR 0004).

## Consequences

- The LED loop is never delayed by GUI rendering.
- Signal lines for the encoder and LEDs stay only a few cm long: low capacitive load
  and fewer EMC issues. Only the slow UART link runs along the scooter.
- core works on its own without the display. Both parts can be developed and tested
  separately.
- A protocol between the two controllers has to be defined and maintained.
- Two firmwares and two boards need power and mounting.
