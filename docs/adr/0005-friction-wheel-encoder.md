# ADR 0005: Friction wheel encoder on the rear tyre

- Status: Proposed
- Date: 2026-11-24

## Context

The distance travelled has to be measured with a resolution of at least one count per
4 mm (finer than the LED pitch of ~6.9 mm, ADR 0003; sub-pixel rendering
benefits from even finer resolution). Options considered:

1. **Friction wheel** pressed against the tyre, mounted on the encoder shaft
2. **Toothed belt** from a pulley attached to the wheel
3. **Magnetic sensing** (magnet ring and Hall sensor, or a magnetic encoder IC such
   as AS5047P)

## Decision (proposed)

Use a spring-loaded, rubber-coated friction wheel (e.g. 20 mm) on the rear tyre,
driving an incremental quadrature encoder. With a ~200 mm wheel this gives a ratio of
about 10:1 without a belt.

## Consequences

- Simple mechanics, no modification of the wheel.
- Resolution is easily sufficient. Worst case at the 4 m/s target speed
  (~200 mm wheel diameter → ~628 mm circumference, 10:1, 600 PPR):
  wheel ~6.4 rev/s, encoder shaft ~64 rev/s (~3800 rpm), ~38 kHz per channel,
  ~150 k counts/s with quadrature x4. This is far below the PCNT limit and fine for
  a glitch filter of up to 1 µs.
- The encoder shaft speed (~3800 rpm at 4 m/s) is close to the limit of some small
  encoders. Choose a model rated ≥ 5000 rpm, or use a larger friction wheel
  (lower ratio).
- May slip in wet or dirty conditions. The effective circumference must be calibrated
  (e.g. ride 10 m and count pulses), because the tyre compresses under load.
- Encoder requirements: check max. shaft speed and output frequency. Open-collector
  outputs need pull-ups (1–4.7 kΩ) to 3.3 V, since the ESP32-S3 is not 5 V tolerant.
- If slip turns out to be a problem, switch to option 2 or 3.
