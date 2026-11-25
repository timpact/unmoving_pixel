# ADR 0003: Clocked LEDs (SK9822)

- Status: Accepted
- Date: 2026-11-24

## Context

For a ground-fixed pattern, the visible error is the distance the scooter travels
between two frames:

    stutter per frame = v / fps

LED density does not help: lower density allows a higher frame rate but makes the
spatial resolution coarser (e.g. 16.7 mm at 60 LEDs/m).

The target speed is 4 m/s. With a 4 mm pitch (125 LEDs on 0.5 m), the stutter should
stay at or below one LED pitch, i.e. ≥ 1000 fps, ideally ~2000 fps (half a pitch).

With single-wire LEDs such as WS2812B (800 kHz, ~30 µs per RGB LED plus ~0.3 ms
reset), one frame takes ~4 ms:

| Setup | max. fps | Stutter at 2 m/s | Stutter at 4 m/s |
|---|---|---|---|
| WS2812B, 1 output | ~245 | ~8 mm | ~16 mm |
| WS2812B, 2 parallel outputs | ~450 | ~4.5 mm | ~9 mm |
| SK9822 via SPI, 125 LEDs | ~2000+ (see below) | ~1 mm | ~2 mm |

SK9822 frame size for 125 LEDs: 32 bit start frame + 125 × 32 bit + end frame
(≥ n/2 clock edges, ~64 bit) ≈ 4100 bit. At 10 MHz SPI clock this is ~0.41 ms,
i.e. ~2400 fps before rendering time.

WS2812B LEDs also dim with a PWM of roughly 1–2 kHz. On a moving strip this shows up
as dashed trails at partial brightness.

## Decision

Use **SK9822** LEDs driven by hardware SPI (with DMA) on the ESP32-S3.
Keep a high LED density (ideally 4–7 mm pitch).

APA102 and HD107S were also considered. SK9822 was chosen because its 5-bit global
brightness adjusts the drive current instead of adding a second, slow PWM stage
(APA102 does that at ~580 Hz, which would cause visible trails in motion).

## Consequences

- ~2000 fps for 125 LEDs, stutter ~2 mm at 4 m/s.
- SK9822 PWM is ~4.7 kHz (lower than APA102). At 4 m/s the strip moves ~0.85 mm per
  PWM period, below the LED pitch, so no visible dashed trails are expected.
- Global brightness (5 bit, current-based) is used for the brightness setting;
  the 8-bit PWM values are kept for the pattern and sub-pixel rendering.
- SK9822 end frame differs from APA102: send at least n/2 extra clock edges
  (use ≥ 64 bits of zeros for 125 LEDs).
- SPI clock: start with 10 MHz and verify signal integrity on the real wiring;
  higher clocks may work with short lines.
- One extra wire (clock) compared to WS2812B.
- Availability of SK9822 strips at 4 mm pitch must be checked. 144 LEDs/m
  (~6.9 mm) is widely available.
- At 4 m/s latency matters: 1 ms of uncompensated latency equals 4 mm (one pitch).
  Latency compensation (`offset += v × latency`) is required.
