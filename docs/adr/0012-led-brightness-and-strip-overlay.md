# ADR 0012: LED brightness and strip overlay

- Status: Accepted
- Date: 2026-11-25 (updated 2026-11-25: profile redefined as strip overlay)

## Context

Besides the ground-fixed pattern (ADR 0011), the rider wants to shape the
brightness along the strip itself, e.g. darker at both ends and bright in the
middle. This makes the moving pattern more pleasant to look at, or is simply
something to play with.

This shaping belongs to the strip, not to the ground: it moves with the scooter,
while the pattern stays on the ground. It is therefore a separate category, the
**overlay**.

SK9822 LEDs (ADR 0003) have 8-bit PWM per colour channel plus a 5-bit global
brightness per LED that adjusts the drive current.

Sliders generate many value changes while being dragged; these must not cause
many flash writes.

## Decision

### Brightness per LED

```
intensity(i) = pattern(i) × overlay(i) × global
```

- `pattern(i)`: colour × segment brightness from the pattern, averaged over the
  LED footprint (ADR 0011)
- `overlay(i)`: factor 0.0 … 1.0, fixed to the strip (this ADR)
- `global`: global brightness slider, 0 … 100 %

### Overlay: edge fade with two S-curves

The overlay has exactly the length of the LED strip and moves with the scooter.
Version 1 has one overlay type, the **edge fade**: the brightness rises from the
start of the strip along an S-curve to the middle level, stays there, and falls
along a second S-curve to the end of the strip.

Parameters:

| Parameter | Type | Meaning |
|---|---|---|
| level_start | uint8 | factor at the first LED, 0 … 100 % |
| level_mid | uint8 | factor in the middle section, 0 … 100 % |
| level_end | uint8 | factor at the last LED, 0 … 100 % |
| fade_start_mm | uint16 | length of the S-curve at the start of the strip |
| fade_end_mm | uint16 | length of the S-curve at the end of the strip |

`fade_start_mm + fade_end_mm ≤ strip length`. If both are half the strip
length, there is no flat middle section.

For an LED at strip position `s` (0 … strip length `L`):

```
smoothstep(t) = 3t² − 2t³

s < fade_start:      t = s / fade_start
                     overlay = level_start + (level_mid − level_start) × smoothstep(t)
s > L − fade_end:    t = (L − s) / fade_end
                     overlay = level_end + (level_mid − level_end) × smoothstep(t)
otherwise:           overlay = level_mid
```

The S-curve is flat at both of its ends, so there is no visible kink where the
fade meets the middle section.

- The overlay is computed into a table (one factor per LED) whenever a parameter
  changes, not per frame.
- The overlay has a `type` field (1 = edge fade), so further overlay types can be
  added later without changing the concept.

### Overlay presets

Like patterns, overlays can be stored as named presets (built-in from the
repository, plus up to 8 user slots on core, ADR 0013). Selecting a preset copies
its parameters into the active overlay; the sliders on the dash then adjust the
active overlay.

### Output to SK9822

- Brightness is applied in linear light, before gamma encoding.
- The resulting per-LED intensity (16-bit internally) is split into the 5-bit
  current setting and the 8-bit PWM values, using the highest PWM range
  possible. This keeps colour resolution at low brightness and avoids visible
  steps when dimming.
- **Current limit:** core estimates the strip current from the frame data and
  scales the brightness down if a configurable limit is exceeded (protects the
  power supply; limit to be set once the supply is chosen).

### Settings handling

- Sliders send their values to core while being dragged (rate-limited on the dash
  to ≤ 20 updates/s). core applies them immediately.
- core saves the settings in its `cfg` NVS partition (ADR 0011) only after the
  values have been unchanged for 2 s (debounce). One drag therefore causes one
  flash write.
- Settings: global brightness, active overlay parameters, active pattern slot.

## Consequences

- Pattern and overlay are independent and can be freely combined.
- Smooth, kink-free fades at the strip ends, adjustable with a few sliders.
- Good low-brightness behaviour thanks to using the SK9822 current setting.
- Brightness changes are visible immediately while dragging, without flash wear.
- If the power fails within 2 s after a change, that change is lost (acceptable).
- The current limit needs the final power supply data (open point).
