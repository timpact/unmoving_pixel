# ADR 0014: Encoder calibration

- Status: Accepted
- Date: 2026-11-25

## Context

core converts encoder counts into distance. The factor depends on the friction
wheel, the tyre and the gear ratio (ADR 0005). It changes over time (wear, tyre
pressure, friction wheel replaced), so calibration is not a one-time step and must
be possible from the dash at any time.

Requirements from the rider:

- Calibration only by a test ride over a known distance; no direct entry of
  wheel diameter or correction factors.
- Distances recorded so far (trip, total) stay unchanged when the factor changes.
- The last 10 calibrations are kept, so that changes (e.g. friction wheel wear)
  can be recognised.

Neither core nor dash has a reliable clock: the P4 RTC keeps time only while
powered, and the JC4880P443 has no battery-backed RTC chip. An external RTC module
may be added later if it brings further benefits.

## Decision

### Calibration factor

- core stores the factor as **nanometres per encoder count** (`nm_per_count`,
  uint32; e.g. 26,180 nm = 0.02618 mm per count). This gives a relative resolution
  better than 0.01 % for the expected encoder setups.
- The factor lives in the `cfg` NVS partition (ADR 0011). Without a stored factor,
  core uses a default from the firmware (derived from the planned mechanics).
- A new factor only affects distance counted from then on. Stored trip and total
  values are not recalculated.

### Test ride

1. On the settings screen the rider presses **"Calibrate"**. core starts counting
   raw encoder counts separately from the odometer (the odometer keeps running
   with the old factor). The count is **net** (forward +, backward −, ADR 0015):
   the rider may ride past the reference mark and roll back.
2. While active, core reports the raw count and the distance according to the
   current factor every 200 ms; the dash shows both.
3. The rider rides or pushes the scooter over a known distance (e.g. 10 m along a
   tape measure) and presses **"Stop"**.
4. The rider enters the true distance in cm on a numeric keypad.
5. The dash shows old factor, new factor and the deviation in %. Plausibility:
   - at least 5 m reference distance and at least 1,000 net counts, otherwise the
     result is rejected (too inaccurate); a negative net count is rejected
   - deviation of more than 10 % from the current factor shows a warning that must
     be confirmed
6. **"Apply"** sends the result to core; core computes and stores the new factor
   and adds an entry to the history. **"Discard"** leaves everything unchanged.

The rider can repeat the test ride several times; each applied result replaces
the factor.

### History

core keeps the last **10** applied calibrations (ring buffer in `cfg`):

| Field | Type | Meaning |
|---|---|---|
| timestamp | uint32 | Unix time (s), 0 = unknown |
| total_m | uint32 | total distance at calibration time |
| ref_cm | uint32 | entered reference distance |
| counts | uint32 | counted net encoder steps (always positive when applied) |
| nm_per_count | uint32 | resulting factor |

The settings screen lists the history (date or "unknown", total distance, factor
change relative to the previous entry in %).

The total distance is always known and is the more meaningful reference for wear
("factor changed by 1.2 % after 800 km"); the date is additional information.

### Date and time

core has no clock; the dash provides the timestamp when the result is applied.
Time sources on the dash, in order of priority:

1. **Battery-backed RTC module**, if one is added later (not present now).
2. **Browser time:** when a phone or laptop opens the preset editor (ADR 0013), the
   web page sends the device time to the dash (`POST /api/time`). No internet
   connection is needed.
3. **Manual entry** of date and time on the settings screen.

Once set, the dash keeps the time in the P4 RTC while powered. After a power loss
without battery-backed RTC, the time is unknown until it is set again; calibrations
are then stored with timestamp 0 ("unknown").

### Protocol (ADR 0004)

The messages are listed in ADR 0004 (CALIB_* and GET_CALIB_HISTORY).

## Consequences

- Calibration can be repeated at any time from the handlebar; no laptop needed.
- Past distances stay as they were recorded.
- The history makes wear and changes visible, also without date via total distance.
- Accuracy depends on the reference distance: 10 m measured to ±1 cm gives
  ±0.1 %; longer distances give better results.
- Dates are only available if a time source was used since the last power-up
  (unless the board has a battery-backed RTC).
