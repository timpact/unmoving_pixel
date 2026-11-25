# ADR 0015: Direction of travel

- Status: Accepted
- Date: 2026-11-25

## Context

The scooter can also move backwards (pushing back, rolling back, correcting an
overshoot). The quadrature encoder (ADR 0005) delivers the direction: the PCNT
counts up and down. The individual functions need different behaviour:

- **LED pattern** (ADR 0011): must stay fixed to the ground in both directions.
- **Trip and total distance** (ADR 0009): like a vehicle odometer, they count the
  distance travelled; backward movement is added, never subtracted.
- **Distance measurements** (ADR 0008): by default backward movement is
  subtracted (net distance, e.g. rolling back after overshooting a line). A
  setting allows adding it instead.
- **Calibration** (ADR 0014): always net distance, so that an overshoot at the end
  of the test ride can be corrected by rolling back.

A scooter standing still can vibrate, so that the encoder toggles by ±1 count
around one position. Counted as travelled distance, this jitter would slowly
increase the odometer while standing.

## Decision

### Two distance values on core

core derives two values from the encoder counts:

| Value | Meaning | Used for |
|---|---|---|
| `position_mm` | **net** position, signed: forward +, backward − | LED pattern offset, net measurements, calibration |
| `total_mm` | **travelled** distance, only increases | trip, total, measurements in "add" mode |

Both are sent as uint32 in the STATUS frame (ADR 0004) and wrap at 2^32.
Differences are computed with unsigned 32-bit arithmetic and interpreted as int32
for `position_mm`; this is correct across a wrap-around for distances below
~2147 km.

`position_mm` is not persisted; after a restart it starts at 0. Measurements only
use differences, so this does not matter as long as no measurement spans a core
restart in net mode (see consequences).

### Hysteresis against jitter

`total_mm` uses a direction hysteresis of **5 mm**:

- While moving in one direction, every millimetre is added.
- A reversal is only accepted after the position has moved at least 5 mm away
  from the turning point in the new direction; then these 5 mm and all further
  movement are added.
- Jitter smaller than 5 mm therefore never increases `total_mm`.

`position_mm` uses the raw counts without hysteresis (jitter cancels out there).

### Speed

- Speed is signed (`int16`, mm/s; forward +, backward −) in the STATUS frame.
- The dash shows the magnitude; while moving backwards the unit line shows
  "rückwärts".
- Stop detection (ADR 0009) uses the magnitude.

### Distance measurements (ADR 0008)

- Setting on the settings screen, stored on the dash:
  **"Rückwärtsfahrt bei Messstrecken: abziehen (default) / addieren"**.
  It applies to both measurements.
- "abziehen" uses `position_mm`, "addieren" uses `total_mm`.
- In net mode the value can become negative (e.g. rolling back right after
  start); it is shown with a minus sign.
- Changing the setting while a measurement runs ends the current interval with
  the old mode and continues with the new one; the value does not jump.

### Calibration (ADR 0014)

- The test ride counts **net** encoder counts (signed, int32).
- The rider may ride past the reference mark and roll back; only the net
  distance between start and stop counts.
- The plausibility check uses the magnitude of the net counts (≥ 1,000); a
  negative result (more backward than forward) is rejected.

## Consequences

- The LED pattern, the odometer and the measurements behave as expected in both
  directions.
- Vibration while standing does not increase the odometer.
- Up to 5 mm of real movement directly after a reversal are delayed (they are
  added once the 5 mm are exceeded), which is negligible.
- If core restarts while a measurement runs in net mode, `position_mm` starts
  again at 0 and the measurement would jump. dash detects the restart (STATUS
  sequence or a restart flag in `error_bits`) and keeps the value by ending the
  interval at the last known position. To be detailed in the dash firmware.
- STATUS frame grows by 4 bytes (`position_mm`).
