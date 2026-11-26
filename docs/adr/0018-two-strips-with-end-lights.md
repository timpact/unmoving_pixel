# ADR 0018: Two LED strips with end lights (turn signals, brake, front and tail light)

- Status: Accepted
- Date: 2026-11-26

## Context

So far one LED strip (~0.5 m, 72 LEDs) shows the ground-fixed pattern (ADR 0003,
0011). The rider wants:

- **Two strips**, one on the left and one on the right side of the scooter, both
  facing the ground and showing **the same pattern**.
- The **ends of both strips bent up by 90°**, so a few LEDs face forward (front
  end) and backward (rear end). These end LEDs do **not** show the pattern. They
  are used as lights:
  - turn signal left or right
  - hazard lights
  - brake light, triggered by deceleration
  - white light to the front (and a red tail light to the back)
- The final strip length is only known once the scooter is there (estimate
  0.5–0.7 m). Length and number of end LEDs must be **set in the dash settings**,
  not compiled in.
- Turn signals and hazard lights are operated by touch on the ride screen; they
  will rarely be used (hazard lights more often than turn signals).

The latency and stutter requirements of ADR 0003 still apply: latency short and
constant, stutter per frame (v / fps) well below the LED pitch of 6.94 mm.

## Decision

### Strip layout

Each strip consists of three parts, in mounting order from front to back:

```
front end      main part (faces the ground)               rear end
(faces forward)                                           (faces backward)
  ┃ n_front ┃━━━━━━━━━━━━━━━━━ n_main ━━━━━━━━━━━━━━━━━━━┃ n_rear ┃
```

| Setting | Range | Default | Meaning |
|---|---|---|---|
| `main_leds` | 36 … 144 | 72 | LEDs of the main part (0.25 … 1.0 m at 144 LEDs/m) |
| `front_leds` | 0 … 8 | 4 | LEDs of the front end |
| `rear_leds` | 0 … 8 | 4 | LEDs of the rear end |
| `data_in_front` (per strip) | yes / no | yes | data input at the front end (no: strip mounted the other way round, LED order is reversed in software) |

- Both strips have the same layout; only the data direction is set per strip.
- Both main parts are assumed to sit at the same longitudinal position, so they
  show the identical frame.
- The settings live on core (single source of truth, `cfg` NVS, saved 2 s after
  the last change, ADR 0012) and are edited on the dash settings screen.
- The main part length is `main_leds × 6.94 mm`. It replaces the fixed 500 mm
  strip length of ADR 0011/0012 (rendering, overlay, editor preview).

### Output: one SPI bus per strip

- The two strips are driven by the two general-purpose SPI controllers of the S3
  (SPI2 and SPI3), each with DMA. 4 GPIOs (data + clock per strip), chosen at
  bring-up; GPIO 35–37 stay forbidden.
- Every frame, the render loop computes the main part **once** and writes it into
  both transmit buffers (reversed for a strip with `data_in_front = no`), adds the
  end LEDs per side, and starts both DMA transfers back to back (a few µs apart).
- Chaining both strips on one bus was rejected: double frame time, a cable across
  the deck, and one defective LED could switch off the other strip.

Frame time per strip (10 MHz SPI, 32 bit start frame, 32 bit per LED, end frame
≥ n/2 bits):

| Layout per strip | LEDs | fps | Stutter at 4 m/s |
|---|---|---|---|
| 72 + 2 × 4 (0.5 m) | 80 | ~3700 | ~1.1 mm |
| 101 + 2 × 8 (0.7 m) | 117 | ~2500 | ~1.6 mm |
| 144 + 2 × 8 (maximum) | 160 | ~1900 | ~2.1 mm |

All stay well below the LED pitch. The frame rate and latency do not depend on
having two strips, only on the LEDs per strip.

**Memory:** transmit buffers are sized for the maximum layout (160 LEDs ≈ 660 bytes
per strip), double-buffered for DMA: ~2.6 KB in total. The pattern stays in RAM
once (max. ~12 KB, ADR 0011). Negligible compared to ~512 KB SRAM.

**CPU:** the box filter runs once for the main part (as before); copying and the
end LEDs cost a few µs.

### Pattern and overlay

- The pattern (ADR 0011) is shown on the main part of both strips only.
- The overlay (ADR 0012) refers to the main part: its length `L` is the main part
  length. Fade lengths are validated against the maximum main part length
  (1000 mm); if `fade_start + fade_end` exceeds the configured `L`, both are
  scaled down proportionally (behaviour of `up_overlay_factor()` already).
- The preset editor takes the main part length from `GET /api/settings` for the
  strip preview and the overlay curve (ADR 0017).

### End lights

Functions and colours:

| Function | Front end | Rear end |
|---|---|---|
| Turn signal left / right | amber, blinking, on that side | amber, blinking, on that side |
| Hazard lights | amber, blinking, both sides in sync | amber, blinking, both sides in sync |
| Light on | white | red tail light (30 % of the light brightness) |
| Brake light | – | red, 100 % of the light brightness |

Priority per end (highest first): turn signal / hazard → brake light → front or
tail light → off. During the dark phase of the blinker the blinking end is off
(contrast), the other side keeps its light.

**All transitions of the end lights are smooth.** Changes between states (light
on/off, brake light, start and end of blinking) fade along an S-curve instead of
switching hard. The curves work on perceptual brightness (converted to linear
light like the segment brightness, ADR 0011), so the fade looks even to the eye.
They are computed per frame on core from the time since the state change; with
~2000–3700 fps the steps are invisible.

- **Blinking:** 1.5 Hz (90 flashes per minute, period ~667 ms), generated on core,
  so it keeps running if the link to the dash fails. The blinking is **smooth, not
  hard on/off**: each flash rises over ~100 ms, stays at full level, and falls
  over ~150 ms, both along an S-curve (smoothstep, as in ADR 0012); about half of
  the period is at full level.
- **Turn signal off automatically** after 30 s or after 10 m travelled since it
  was switched on, whichever comes first. Tapping the same button again switches
  it off; tapping the other side switches over. Hazard lights stay on until
  switched off.
- **Brake light** detected on core from its own speed (no UART delay):
  - deceleration = decrease of |speed| (works when riding backward too), from
    the speed smoothed over ~100 ms (the raw encoder signal is too noisy to
    differentiate)
  - on when the deceleration is ≥ threshold (default 1.0 m/s², setting
    0.5 … 3.0 m/s²) for ≥ 50 ms at a speed above 0.5 m/s
  - stays on for at least 1 s; off when the deceleration falls below half the
    threshold
  - **switched on smoothly:** the brightness rises along an S-curve over ~100 ms
    (from off, or from the tail light level to 100 %), and falls back over
    ~200 ms when the brake light goes off
  - works independently of "light on" and can be disabled in the settings
  - reaction time ~100–150 ms until the brake light starts to rise (dominated by
    the speed smoothing), full brightness ~100 ms later
- **Light brightness:** separate setting (10 … 100 %), independent of the global
  pattern brightness and the overlay.
- **Current limit (ADR 0012):** the limit dims the pattern first; the end lights
  are not dimmed by it.
- Switching states are **not stored**: after a restart turn signal, hazard lights
  and "light on" are off. Switching them must not cause flash writes. Only the
  rarely changed settings (light brightness, brake light on/off, brake threshold)
  are stored in `cfg` (debounced 2 s, ADR 0012).

These lights are additional lights without type approval; they do not replace
required lighting.

### dash GUI

- **Ride screen:** the area of the two distance measurements becomes switchable
  with a segmented control **"Messung | Licht"** above it (the choice is stored
  on the dash):
  - "Messung": the two distance measurements as before (ADR 0008)
  - "Licht": three large buttons "◄ Blinker links", "Warnblinker",
    "Blinker rechts ►", and a switch "Licht"
  - A **turn signal indicator** (arrows left/right in `warn` colour, blinking in
    phase with core) is always visible near the speed, also while "Messung" is
    selected.
- **Settings screen**, new cards:
  - "LED-Streifen": main part in LEDs (with the resulting length in cm), LEDs at
    the front end, LEDs at the rear end, data input front/rear for the left and
    the right strip
  - "Licht": light brightness, brake light on/off, brake threshold

### Protocol (changes to ADR 0004)

- STATUS gets one byte `lights` (27 → 28 bytes): bits 0–1 signal (0 off, 1 left,
  2 right, 3 hazard), bit 2 blink phase on, bit 3 brake light on, bit 4 light on.
- SETTINGS gets the strip layout (`main_leds`, `front_leds`, `rear_leds`,
  `layout_flags` with the data direction per strip) and the light settings
  (`light_on`, `light_brightness`, `brake_flags`, `brake_threshold` in
  0.1 m/s²).
- New commands:

  | Type | Name | Payload after `seq` |
  |---|---|---|
  | 0x92 | SET_SIGNAL | uint8: 0 off, 1 left, 2 right, 3 hazard |
  | 0x93 | SET_LIGHT | uint8: 0 off, 1 on |
  | 0x94 | SET_STRIP_LAYOUT | `main_leds`, `front_leds`, `rear_leds`, `layout_flags` (uint8 each) |
  | 0x95 | SET_LIGHT_SETTINGS | `light_brightness`, `brake_flags`, `brake_threshold` (uint8 each) |

- `protocol_version` is incremented.

## Consequences

- Twice the visible effect with unchanged latency and stutter; no noticeable
  memory or CPU cost.
- Strip length and end LEDs can be adjusted on the scooter without rebuilding the
  firmware.
- Turn signals, hazard lights and brake light run on core and keep working
  without the dash (except switching them on).
- 4 GPIOs on core for the LEDs instead of 2.
- Twice the maximum LED current; the current limit and the power supply
  (open point) must cover both strips plus the end lights.
- Changes to existing ADRs once accepted: ADR 0003 (two buses, variable length),
  ADR 0004 (messages above), ADR 0010 (ride and settings screens), ADR 0011
  (main part length instead of 500 mm), ADR 0012 (overlay length and fade
  limits), ADR 0017 (editor preview uses the configured length).
- Mechanics: bend the strip over its flat side, not directly at an LED; more
  durable: cut at the solder pads between LEDs and connect the ends with short
  wires.
