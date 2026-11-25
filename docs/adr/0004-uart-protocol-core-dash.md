# ADR 0004: UART protocol between core and dash

- Status: Accepted
- Date: 2026-11-24

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
- CRC16 over type, length and payload
- Frames with a wrong CRC are dropped silently

### Messages

core → dash, every 50 ms:

| Field | Type | Unit |
|---|---|---|
| speed | uint16 | mm/s |
| trip distance | uint32 | m |
| total distance | uint32 | m |
| total_mm | uint32 | mm, total distance, wraps at 2^32 (ADR 0008) |
| status | struct | active pattern, error bits, battery voltage, NVS write counter (ADR 0009) |

dash → core, on demand:

- set pattern
- set brightness
- reset trip
- save odometer now (ADR 0009)

core answers each command with an ACK.

### Ownership and failure behaviour

- core is the single source of truth. It stores odometer values in its own NVS.
  dash only displays them.
- If dash receives no valid frame for 500 ms, it shows "--" instead of stale values.
- core keeps running unchanged if dash is missing or the link fails.

## Consequences

- Simple and robust; easy to debug with a USB-UART adapter.
- The frame definition lives in `common/` and is shared by both firmwares.
- No conflicting odometer values, since only core stores them.
- Adding fields requires a protocol version or new message types.
