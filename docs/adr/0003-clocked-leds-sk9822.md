# ADR 0003: Clocked LEDs (SK9822), 144 LEDs/m

- Status: Accepted
- Date: 2026-11-24 (updated 2026-11-25: 144 LEDs/m strip ordered)

## Context

For a ground-fixed pattern, the visible error is the distance the scooter travels
between two frames:

    stutter per frame = v / fps

LED density alone does not help: lower density allows a higher frame rate but makes
the spatial resolution coarser (e.g. 16.7 mm at 60 LEDs/m). With clocked LEDs the
frame rate is high enough at any common density, so the density can be chosen for
the look and for availability.

The target speed is 4 m/s. The stutter should stay well below one LED pitch.

With single-wire LEDs such as WS2812B (800 kHz, ~30 µs per RGB LED plus ~0.3 ms
reset), the frame rate is limited. Comparison for a 0.5 m strip:

| Setup | max. fps | Stutter at 2 m/s | Stutter at 4 m/s |
|---|---|---|---|
| WS2812B, 4 mm pitch (125 LEDs), 1 output | ~245 | ~8 mm | ~16 mm |
| WS2812B, 4 mm pitch, 2 parallel outputs | ~450 | ~4.5 mm | ~9 mm |
| SK9822, 4 mm pitch (125 LEDs), SPI 10 MHz | ~2400 | ~1 mm | ~2 mm |
| **SK9822, 144 LEDs/m (72 LEDs), SPI 10 MHz** | **~4000** | **~0.5 mm** | **~1 mm** |

SK9822 frame size for 72 LEDs: 32 bit start frame + 72 × 32 bit + end frame
(≥ n/2 clock edges, use 64 bit) ≈ 2430 bit. At 10 MHz SPI clock this is ~0.24 ms,
i.e. ~4000 fps before rendering time.

WS2812B LEDs also dim with a PWM of roughly 1–2 kHz. On a moving strip this shows up
as dashed trails at partial brightness.

SK9822 strips with 4 mm pitch are hard to get; 144 LEDs/m is widely available.

## Decision

Use **SK9822** LEDs with **144 LEDs/m** (pitch ~6.94 mm, 72 LEDs on 0.5 m), driven
by hardware SPI (with DMA) on the ESP32-S3.

APA102 and HD107S were also considered. SK9822 was chosen because its 5-bit global
brightness adjusts the drive current instead of adding a second, slow PWM stage
(APA102 does that at ~580 Hz, which would cause visible trails in motion).

## Consequences

- ~4000 fps for 72 LEDs, stutter ~1 mm at 4 m/s (well below the 6.9 mm pitch).
- Sub-pixel rendering (ADR 0011) is essential at this pitch: without it, edges
  would jump in 6.9 mm steps.
- Pattern resolution of 1 cm (ADR 0011) corresponds to ~1.4 LEDs, i.e. close to
  the LED grid.
- SK9822 PWM is ~4.7 kHz (lower than APA102). At 4 m/s the strip moves ~0.85 mm per
  PWM period, well below the LED pitch, so no visible dashed trails are expected.
- The 5-bit current setting and the 8-bit PWM are combined for brightness
  (ADR 0012).
- SK9822 end frame differs from APA102: send at least n/2 extra clock edges
  (use 64 bits of zeros for 72 LEDs).
- SPI clock: start with 10 MHz and verify signal integrity on the real wiring;
  higher clocks may work with short lines.
- One extra wire (clock) compared to WS2812B.
- Latency τ (encoder reading → light) shifts the pattern on the ground by v × τ.
  It does not accumulate: every frame starts from the absolute encoder count.
  - At constant speed the shift is constant, so the pattern still stands still on
    the ground (just displaced, e.g. 4 mm at 4 m/s and 1 ms). This is invisible.
  - When accelerating or braking the shift changes by Δv × τ: the pattern slides
    slightly, e.g. 4 mm in total from 0 to 4 m/s at τ = 1 ms (hardly visible).
  - **Varying latency (jitter) is what becomes visible:** the shift then changes
    from frame to frame and the pattern trembles (0.8 ms jitter = 3.2 mm at 4 m/s).
- Therefore the render loop must keep the latency **short (< 1 ms) and constant**:
  sample the encoder at a fixed point relative to the output, use a fixed frame
  timing and SPI with DMA, and keep UART handling and flash writes from delaying
  frames.
- Latency compensation (`offset += v × τ`) is optional. It only matters for
  acceleration and braking, and a noisy speed value would itself add jitter. If it
  is added, use a smoothed speed.
- The existing 4 mm WS2812B strip can still be used for early prototyping, with
  reduced quality at higher speeds.
