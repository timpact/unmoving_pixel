# ADR 0004: UART protocol between core and dash

- Status: Accepted
- Date: 2026-11-24 (updated 2026-11-25: full message catalogue;
  2026-11-26: lights and strip layout, ADR 0018; encoding details for the
  implementation in common/)

## Context

core (ESP32-S3, deck) and dash (ESP32-P4, handlebar) are about 1 m apart (ADR 0001).
Only a small amount of data is exchanged: a status update every 50 ms and occasional
user commands. The link runs next to LED supply lines on a vibrating vehicle.

## Decision

### Physical layer

- UART, 3.3 V, 115200 baud, 8N1
- Twisted wires with GND, 100 Ω series resistor on each TX
- Pins on dash: see ADR 0006
- Fallback if problems occur: RS-485 with 3.3 V transceivers (MAX3485/SP3485)
- Both boards powered from the same source, star-shaped ground

### Frame format

```
| 0xAA 0x55 | type (1) | length (1) | payload (n) | CRC16 (2) |
```

- Little-endian
- CRC16 over type, length and payload: CRC-16/CCITT-FALSE (polynomial 0x1021,
  initial value 0xFFFF, no reflection, no final XOR; check value for "123456789"
  is 0x29B1)
- Frames with a wrong CRC are dropped silently
- Maximum frame size: 2 + 1 + 1 + 255 + 2 = 261 bytes
- The receiver drops a partly received frame if no byte arrives for 20 ms and
  searches for the next `0xAA 0x55`
- A message whose payload length does not match its type is ignored (commands
  are answered with ACK status "invalid")

### Messages

Payload length is at most 255 bytes (1-byte length field). Types 0x01–0x7F are
sent by core, 0x80–0xFF by dash.

#### core → dash

| Type | Name | When | Payload |
|---|---|---|---|
| 0x01 | STATUS | every 50 ms | see below (28 bytes) |
| 0x02 | SETTINGS | at start, on every change, on request | see below |
| 0x03 | PRESET_LIST | on request | `category`, `total` (entries in the whole list), `index` (of the first entry in this frame), `count` (entries in this frame), then up to 10 entries |
| 0x04 | PRESET_HEADER | on request (GET_PRESET) | `category`, `slot`, `revision` (uint16), preset header |
| 0x05 | PRESET_CHUNK | after PRESET_HEADER | `chunk_index` (uint16), data (≤ 240 bytes) |
| 0x06 | CALIB_STATUS | every 200 ms while a calibration is active, and after stop | `state` (uint8: idle, counting, stopped), `counts` (int32, net), distance with current factor (int32, mm) (ADR 0014, 0015) |
| 0x07 | CALIB_HISTORY | on request and after CALIB_APPLY | current `nm_per_count` (uint32), `count`, then up to 10 entries of 20 bytes (ADR 0014) |
| 0x7F | ACK | answer to every command | `seq`, `status` (see below) |

STATUS payload:

| Field | Type | Unit / meaning |
|---|---|---|
| speed | int16 | mm/s, forward +, backward − (ADR 0015) |
| trip distance | uint32 | m |
| total distance | uint32 | m |
| total_mm | uint32 | mm, travelled distance (backward added, with hysteresis), wraps at 2^32 (ADR 0008, 0015) |
| position_mm | uint32 | mm, net position (backward subtracted), wraps at 2^32, interpret differences as int32 (ADR 0015) |
| active_pattern | uint8 | active pattern slot |
| error_bits | uint16 | error flags |
| battery | uint16 | mV |
| nvs_writes | uint32 | odometer write counter (ADR 0009) |
| lights | uint8 | bits 0–1 signal (0 off, 1 left, 2 right, 3 hazard), bit 2 blink phase on, bit 3 brake light on, bit 4 light on (ADR 0018) |

SETTINGS payload:

| Field | Type | Meaning |
|---|---|---|
| protocol_version | uint8 | protocol version (dash shows an error on mismatch) |
| global_brightness | uint8 | 0 … 100 % (ADR 0012) |
| overlay | 8 bytes | active overlay: type, level_start, level_mid, level_end (uint8 each), fade_start_mm, fade_end_mm (uint16 each) (ADR 0012) |
| active_pattern | uint8 | active pattern slot |
| active_overlay | uint8 | active overlay preset slot (ADR 0012, 0017) |
| preset_list_rev | uint16 | incremented whenever presets are stored or deleted |
| main_leds | uint8 | LEDs of the main part, 36 … 144 (ADR 0018) |
| front_leds | uint8 | LEDs of the front end, 0 … 8 |
| rear_leds | uint8 | LEDs of the rear end, 0 … 8 |
| layout_flags | uint8 | bit 0: left strip data input at the front, bit 1: right strip data input at the front |
| light_on | uint8 | 0 off, 1 on (not stored, ADR 0018) |
| light_brightness | uint8 | 10 … 100 % |
| brake_flags | uint8 | bit 0: brake light enabled |
| brake_threshold | uint8 | deceleration threshold in 0.1 m/s², 5 … 30 |

Preset categories: 0 = pattern (ADR 0011), 1 = overlay (ADR 0012).

ACK status codes:

| Code | Meaning |
|---|---|
| 0 | OK |
| 1 | unknown command |
| 2 | invalid (wrong payload length or value out of range) |
| 3 | revision conflict (PRESET_BEGIN, ADR 0017) |
| 4 | slot read-only (built-in preset) |
| 5 | no free slot |
| 6 | not found (empty slot) |
| 7 | busy (another preset transfer is running) |
| 8 | CRC mismatch (PRESET_COMMIT) |
| 9 | out of sequence (chunk index or commit without BEGIN) |

Preset headers:

- Pattern: the pattern header of ADR 0011 (23 bytes: `format_version`, `name`,
  `segment_count`, `crc32`), followed by the segments in PRESET_CHUNKs.
  CRC32: CRC-32/ISO-HDLC (as in zlib, check value 0xCBF43926) over the header
  without the `crc32` field and all segments.
- Overlay (25 bytes): `format_version` (uint8, 1), `name` (16 bytes, UTF-8,
  zero-padded), overlay parameters (8 bytes, as in SETTINGS). No chunks follow;
  the frame CRC protects it.

PRESET_LIST entry (24 bytes): `slot` (uint8), `flags` (uint8: built-in, user,
empty), `revision` (uint16, incremented on every write of this slot, ADR 0017),
`length_cm` (uint32, total pattern length, patterns only, else 0),
`name` (16 bytes).

Preset data in PRESET_HEADER/PRESET_CHUNK and PRESET_BEGIN/PRESET_CHUNK:
patterns are sent as header plus segments (6 bytes each, 40 per chunk,
ADR 0011); overlays fit completely into the header (no chunks).

#### dash → core

Every command starts with a sequence number `seq` (uint8). core answers with an
ACK carrying the same `seq`.

| Type | Name | Payload after `seq` |
|---|---|---|
| 0x81 | SET_BRIGHTNESS | global (uint8) |
| 0x82 | SET_OVERLAY | overlay parameters (8 bytes, as in SETTINGS); core writes them into the active overlay preset (debounced 2 s); a built-in preset is copied to a new user slot first (ADR 0012) |
| 0x83 | SELECT_PRESET | category, slot |
| 0x84 | RESET_TRIP | – |
| 0x85 | SAVE_ODOMETER | – (ADR 0009) |
| 0x86 | GET_SETTINGS | – |
| 0x87 | GET_PRESET_LIST | category |
| 0x88 | GET_PRESET | category, slot |
| 0x89 | PRESET_BEGIN | category, target slot (0xFF = preview, not stored), expected revision (uint16, 0xFFFF = new slot), preset header |
| 0x8A | PRESET_CHUNK | `chunk_index` (uint16), data (≤ 240 bytes) |
| 0x8B | PRESET_COMMIT | – (core checks CRC32, stores or previews, ACKs) |
| 0x8C | PRESET_DELETE | category, slot |
| 0x8D | CALIB_START | – (ADR 0014) |
| 0x8E | CALIB_STOP | – |
| 0x8F | CALIB_APPLY | `ref_cm` (uint32), `timestamp` (uint32, Unix time, 0 = unknown) |
| 0x90 | CALIB_DISCARD | – |
| 0x91 | GET_CALIB_HISTORY | – |
| 0x92 | SET_SIGNAL | uint8: 0 off, 1 left, 2 right, 3 hazard (ADR 0018) |
| 0x93 | SET_LIGHT | uint8: 0 off, 1 on |
| 0x94 | SET_STRIP_LAYOUT | `main_leds`, `front_leds`, `rear_leds`, `layout_flags` (uint8 each) |
| 0x95 | SET_LIGHT_SETTINGS | `light_brightness`, `brake_flags`, `brake_threshold` (uint8 each) |

#### Reliability

- dash repeats a command if no ACK arrives within 100 ms (max. 3 attempts).
  core detects repeated commands by `seq` and does not apply them twice.
- Preset transfer is stop-and-wait: each chunk is ACKed before the next one is
  sent. A full pattern of 2048 segments (52 chunks, ~12 KB) takes ~1.5 s; a
  typical pattern with a few dozen segments well below 0.1 s.
- STATUS frames continue during preset transfers.
- Slider commands are rate-limited on the dash (≤ 20/s, ADR 0012).
- Link load: STATUS frame = 34 bytes; at 20 Hz ≈ 6.8 kbit/s (~6 % of 115200 baud);
  commands and preset transfers are short bursts.

### Ownership and failure behaviour

- core is the single source of truth. It stores odometer values, LED settings
  and presets in its own NVS. dash only displays them and sends commands.
- If dash receives no valid frame for 500 ms, it shows "--" instead of stale values.
- core keeps running unchanged if dash is missing or the link fails.

## Consequences

- Simple and robust; easy to debug with a USB-UART adapter.
- The frame definition lives in `common/` (`up_proto.h`, `up_proto.c`) and is
  shared by both firmwares. The first implemented version is
  `protocol_version = 1`.
- No conflicting odometer values, since only core stores them.
- Adding fields requires a new `protocol_version` or new message types.
- The 1-byte length field limits payloads to 255 bytes; larger data (patterns)
  is transferred in chunks.
