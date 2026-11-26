# ADR 0011: LED pattern model

- Status: Accepted
- Date: 2026-11-25 (updated 2026-11-25: segments with colour and brightness,
  lengths in cm, up to 2048 segments, no global colour; transitions moved to the
  overlay, ADR 0012; 2026-11-26: two strips, main part length configurable, ADR 0018)

## Context

The LED strip shows a pattern that is fixed to the ground (ADR 0003): the scooter
moves over a virtual "carpet" of colour, and the strip shows the window of it
that is currently under the scooter (main part of each strip, 0.5 m by default,
36 … 144 LEDs set on the dash; both strips show the same window, ADR 0018). Requirements:

- There is only one kind of pattern: a user-defined (or built-in) sequence that
  repeats endlessly along the ground after its end.
- A pattern is defined by lengths on the ground, not by LEDs. The ground pattern
  is effectively continuous; the LED pitch (~6.9 mm, 144 LEDs/m, ADR 0003) is only
  how it is sampled.
- Each part of the pattern has its own colour and brightness. Every colour is set
  explicitly in the pattern (no global colour that recolours patterns; it would
  only add confusion, and full control is possible without it).
- Patterns may be long (10 m and more) and still detailed.
- A minimum segment length of 1 cm is sufficient; segments longer than ~30 m are
  rarely needed.
- Effects that belong to the strip rather than to the ground (e.g. darker strip
  ends) are not part of the pattern; they are handled by the overlay (ADR 0012).

## Decision

### Pattern = list of segments

A pattern is a list of segments with hard edges between them. The pattern length
is the sum of all segment lengths. Along the ground, the pattern repeats:
`position_in_pattern = ground_position mod pattern_length`.

Segment (6 bytes, packed, little-endian):

| Field | Type | Meaning |
|---|---|---|
| length_cm | uint16 | length on the ground, 1 … 65535 cm (max. ~655 m) |
| brightness | uint8 | 0 … 100 % (0 = off) |
| r, g, b | 3 × uint8 | colour (sRGB) |

- Segments have hard edges. Soft-looking gradients can be built from several
  short segments. Smooth motion is provided by the rendering (box filter, see
  below), not by the pattern.
- Longer uniform areas than 655 m can be built from several segments.
- With 1 cm resolution, patterns cannot address single LEDs exactly (1 cm ≈
  1.4 LEDs at 6.9 mm pitch). The box filter still renders them cleanly.

Pattern header (23 bytes):

| Field | Type | Meaning |
|---|---|---|
| format_version | uint8 | 1 |
| name | 16 bytes | UTF-8, zero-padded |
| segment_count | uint16 | 1 … 2048 |
| crc32 | uint32 | over header (without crc) and segments |

### Limits and storage

- Max. 2048 segments per pattern → max. ~12 KB per pattern.
  Example: 10 m with an average segment length of 5 mm is not possible, but
  10 m with an average of 5 cm needs only 200 segments.
- Max. pattern length: 2048 × 655 m (practically unlimited).
- **Built-in patterns:** read-only, compiled into the core firmware from the preset
  files in the repository (ADR 0013), so the strip works out of the box.
- **User patterns:** up to 32 slots, stored on core in a separate NVS partition
  `cfg` (512 KB), not in the odometer NVS (ADR 0009). Settings and overlays
  (ADR 0012) also live in `cfg`. Worst case for 32 full patterns: ~390 KB.
- The active pattern is kept in RAM (max. ~12 KB; the S3 has ~512 KB SRAM).
- Patterns are only written when the user saves them (rare), so flash wear is
  negligible.
- **Preview slot:** one additional pattern slot in RAM only. The editor can send
  a pattern there to try it on the real strip without writing flash.

### Rendering on core

For each frame and each LED `i` of the main part (computed once, used for both
strips, ADR 0018):

1. Ground position of the LED: `x = offset + i × pitch` (offset from the encoder,
   optionally with latency compensation, ADR 0003).
2. Pattern value = **average of colour × brightness over the LED footprint**
   `[x, x + pitch)` (box filter, anti-aliasing). Edges between segments thus move
   smoothly from LED to LED (sub-pixel motion), and segments shorter than one
   LED are shown with their correct share.
3. The segment walk is incremental: LEDs are processed in order, so the current
   segment is carried over from one LED to the next (no search per LED). The cost
   per frame does not depend on the number of segments.
4. Averaging is done in linear light; segment brightness is converted from its
   perceptual percentage to linear light first.
5. The overlay factor and the global brightness are applied afterwards (ADR 0012),
   then the result is gamma-encoded and sent to the strip.

## Consequences

- One simple, compact format with byte-aligned fields covers stripes, dashes,
  dot patterns and detailed long patterns.
- Pattern (ground) and overlay (strip) are independent: any pattern can be
  combined with any overlay.
- Memory use is bounded (~12 KB per pattern, ~390 KB for 32 full patterns).
- Rendering cost is bounded by the LED count per frame, not by pattern length or
  segment count.
- Detail below 1 cm cannot be defined; detail below the LED pitch is averaged.
- Transferring a full pattern (2048 segments) over UART takes ~1.5 s (ADR 0004);
  this only happens when saving from the editor.
- The format is versioned; extensions need `format_version = 2`.
- The same format is used in the UART protocol (ADR 0004) and, as JSON, in the
  editor and the repository presets (ADR 0013).
