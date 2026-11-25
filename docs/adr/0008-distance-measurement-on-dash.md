# ADR 0008: Distance measurement on dash

- Status: Accepted
- Date: 2026-11-25 (updated 2026-11-25: stopwatch behaviour)

## Context

In addition to trip and total distance, the dash should offer a distance
measurement: the rider presses a button and the dash shows the distance travelled
since then, counting up from 0 in metres (with sub-metre resolution if useful).

The status frame (ADR 0004) only carries distances in whole metres. Options
considered:

1. **Pulse line** from core to dash, counted on the dash. Rejected: an extra wire
   over ~1 m (EMC), duplicates the PCNT counting on core, and lost pulses (e.g. dash
   reset) would go unnoticed.
2. **Measurement on core**, started/reset by commands, stored in core NVS.
   Works, but adds commands and state to core for a pure display feature.
3. **Absolute high-resolution distance in the status frame**, measurement computed
   on dash.

Data rate is not a constraint: a status frame with an extra uint32 is ~24 bytes,
i.e. ~4 % of the link at 20 Hz and ~21 % at 100 Hz (115200 baud, 8N1).

## Decision

Use option 3.

- core sends the total distance in millimetres as `total_mm` (uint32, wraps at
  2^32 mm ≈ 4295 km) in every status frame. It is derived from the persistent total
  distance, so it survives power loss (with the loss described in ADR 0009).
- The measurement works like a stopwatch, with three buttons:
  - **Start:** counting starts (or resumes after a stop).
  - **Stop:** the value is frozen. Distance travelled while stopped (e.g. pushing
    the scooter) is not counted.
  - **Reset:** sets the value to 0 (allowed while running or stopped).
- dash keeps the measurement state and stores it in its own NVS on every button
  press (and only then):

  | Field | Type | Meaning |
  |---|---|---|
  | running | bool | measurement is counting |
  | accumulated_mm | uint32 | distance of all finished intervals |
  | start_mm | uint32 | `total_mm` at the last start (valid while running) |

- Displayed value:
  `measured = accumulated_mm + (running ? total_mm − start_mm : 0)`,
  using unsigned 32-bit arithmetic, so a wrap-around of `total_mm` is handled
  automatically (valid for measurements < 4295 km).
- On stop: `accumulated_mm += total_mm − start_mm`, `running = false`.
- If the power fails while running, the measurement continues after restart:
  `start_mm` and `total_mm` both survive (ADR 0009).
- Plausibility check: if `measured` is implausibly large (e.g. core was replaced or
  its NVS was erased, so `total_mm` jumped backwards), dash resets the measurement
  to 0.
- Optional smoothing: between two frames, dash may extrapolate with the current
  speed for a smoother display. The next frame always overrides the estimate.

## Consequences

- No new command and no extra state on core. core stays the single source of
  truth for distance; dash only stores the measurement state (start, stop, sum).
- The measurement survives power loss on both sides.
- Lost frames do not cause errors: every frame carries the absolute value.
- dash writes to its flash only once per button press (negligible wear).
- Pushing the scooter or short breaks can be excluded by pressing stop.
- Several independent measurements (e.g. "lap" and "ride") are possible on dash
  without protocol changes.
- At 20 Hz and 4 m/s the raw value advances in 20 cm steps; smoothing or a higher
  frame rate can be added if needed.
