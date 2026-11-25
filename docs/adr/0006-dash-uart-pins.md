# ADR 0006: UART pins on dash (JC4880P443)

- Status: Accepted
- Date: 2026-11-24

## Context

The UART link to core (ADR 0004) needs two free GPIOs on the ESP32-P4 of the
JC4880P443 board. Most P4 GPIOs are already used by the display, touch, audio,
camera and SD card.

Options considered:

1. **UART0 of the ESP32-C6** (C6 GPIO32 / GPIO34). These are pins of the Wi-Fi
   co-processor, not of the P4. The P4 could only use them by running relay firmware
   on the C6 and forwarding the data over the P4–C6 link. This adds a second firmware
   and extra latency for no benefit.
2. **UART0 of the P4.** The ROM bootloader prints its boot log on UART0 TX at every
   reset, the ESP-IDF console would have to be moved, and UART0 may be connected to
   the board's USB-UART bridge.
3. **Free P4 GPIOs (GPIO31 / GPIO33)** with a free UART peripheral routed through
   the GPIO matrix.

## Decision

Use option 3: P4 GPIO31 and GPIO33 with **UART1**.

| P4 GPIO | Role on P4 | Connected to |
|---|---|---|
| GPIO33 | RX | core TX |
| GPIO31 | TX | core RX |

The TX/RX assignment is set in software (GPIO matrix) and can be swapped if the
wiring requires it.

## Consequences

- UART0 of the P4 stays free for the bootloader, flashing and the console.
  No boot log noise on the link to core.
- The C6 stays untouched and keeps its UART0 for its own flashing/debugging.
  Wi-Fi remains optional.
- No relay firmware on the C6 and no extra latency.
- GPIO31/GPIO33 are no longer available for other functions on the dash board.
