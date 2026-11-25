# ADR 0002: Custom firmware instead of WLED

- Status: Accepted
- Date: 2026-11-24

## Context

The project started with a WLED-controlled strip. WLED effects are time-based: the
speed slider sets how fast an animation runs over time. There is no built-in effect
whose position follows an external encoder. The default frame limit is 42 fps, and
the effect speed cannot be mapped to m/s.

The core requirement is a pattern whose position equals the distance travelled,
updated as fast as the LED protocol allows, with short and constant latency and sub-pixel
rendering.

## Decision

Write custom firmware for the ESP32-S3 using ESP-IDF directly (PCNT for the encoder,
SPI/RMT for the LEDs), without WLED.

A WLED usermod was considered but rejected: it would still be bound to WLED's render
loop and frame limit.

## Consequences

- Full control over frame timing, latency and rendering.
- WLED's ready-made effects, web UI and app are not available. Patterns must be
  implemented in the firmware; selection happens via the dash display.
- More firmware work up front.
