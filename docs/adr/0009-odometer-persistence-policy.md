# ADR 0009: Odometer persistence policy on core

- Status: Accepted
- Date: 2026-11-25

## Context

core stores trip and total distance in its NVS (ADR 0004). The previous rule was
"write every ~100 m or when stationary". Writing on every stop makes the number of
flash writes depend on riding behaviour (many short stops → many writes), so the
flash lifetime is not predictable. Cheap modules may also have flash of unknown
quality (see ADR 0007; saving settings in WLED was unreliable on these boards).

### Wear estimate

- NVS works on 4 KB flash pages with 126 entries of 32 bytes each. A page is only
  erased when it is full and has been garbage-collected.
- One save of the odometer record (trip, total, write counter as one blob) uses
  about 3 entries → ~40 saves per page.
- With an NVS partition of 64 KB (16 pages), NVS spreads the writes over all pages:
  each page is erased roughly once every ~600 saves.
- Typical flash endurance is ≥ 100,000 erase cycles per sector
  → ~60 million saves.

| Save interval | Saves per 1000 km | Lifetime (≈ 60 M saves) |
|---|---|---|
| every 100 m | 10,000 | ~6 million km |
| every 10 m | 100,000 | ~600,000 km |

These are order-of-magnitude figures, but they show that flash wear is not a
limiting factor as long as the number of writes is bounded by distance.

## Decision

1. **Distance-based saving:** core saves the odometer record every 100 m of
   travel (configurable constant).
2. **Saving on stop, bounded by distance:** when the scooter comes to a stop, core
   also saves, but only if at least 10 m were travelled since the last save. This
   keeps the loss on power-off small, and the number of writes stays bounded by
   distance (at most one write per 10 m).
3. **Manual saving:** dash can send a "save now" command; core saves immediately
   (and ACKs). Also bounded: ignored if nothing changed since the last save.
4. **Direction:** trip and total count the travelled distance; backward movement
   is added, with a 5 mm direction hysteresis against jitter (ADR 0015).
5. **Write counter:** the odometer record contains a write counter that is
   incremented on every save. It is stored in the same blob (no extra write) and
   reported to dash for diagnostics.
6. **NVS partition:** 64 KB in the custom partition table.

## Consequences

- The number of flash writes depends only on distance travelled, never on time or
  on the number of stops. Lifetime is predictable and far beyond the scooter's.
- On power loss while riding, up to 100 m are lost; after a stop, at most 10 m.
  Trip, total and the distance measurement of ADR 0008 lose the same amount, so
  they stay consistent.
- The write counter makes actual wear visible; it can be compared with the
  estimate above.
- Flash endurance test (`core/test/flash_endurance`, 2026-11-25) on a core board
  (flash: Macronix, ID 0xC22018, 16 MB): 50 rounds of raw erase/write/verify on
  1 MB plus 25,288 NVS saves, with several power cuts during the test. Result:
  0 errors; after every power cut the stored record was consistent. By the rule of
  three, the failure rate is below 1 in ~8,400 saves (~2,500 km at one save per
  100 m). Not yet tested: voltage dips caused by the LED strip under full load.
- Possible later improvement: detect supply loss (voltage monitoring with a
  hold-up capacitor) and save just before power is gone.
