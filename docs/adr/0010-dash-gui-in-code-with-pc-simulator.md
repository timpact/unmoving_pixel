# ADR 0010: dash GUI defined in code, with a PC simulator

- Status: Accepted
- Date: 2026-11-25 (updated 2026-11-25: settings screen, code structure and
  fonts after the first simulator tests)

## Context

The dash (ESP32-P4, 480×800 MIPI-DSI, GT911 touch, ADR 0001) needs a touch GUI.
LVGL is the standard GUI library for this class of device and is supported by
Espressif (`esp_lvgl_port`).

Options for creating the GUI:

1. **SquareLine Studio** (graphical editor, exports C code). Tried and rejected:
   poor usability, and the board could not be configured in it.
2. **LVGL Editor / XML-based UI.** Adds a second description language and tooling.
3. **GUI written directly in C** against the LVGL API.

Flashing the P4 for every GUI change is slow. A faster preview is needed.

## Decision

### GUI in code

- The GUI is written in C against the LVGL API (LVGL 9.x). No GUI designer,
  no generated code.
- The GUI code is platform-independent: it only uses LVGL and a small data
  interface (see below), never ESP-IDF APIs directly.

### Code structure

```
dash/
├── ui/      # GUI: screens, widgets, styles, font asset – LVGL only, no ESP-IDF
├── app/     # platform-independent dash logic (e.g. distance measurement)
├── main/    # ESP-IDF app: display + touch drivers, UART link, NVS, Wi-Fi
└── sim/     # PC simulator: SDL2 window, simulated core, screenshots
common/include/   # code shared with core (e.g. overlay formula, ADR 0012)
```

- `ui/` talks to the rest of the system through a data interface:
  - **Model in:** a struct with the current values (speed, distances, `total_mm`,
    settings, pattern list, link state). The app (or the simulator) updates it,
    the GUI reads it periodically (e.g. every 50 ms) and refreshes the widgets.
  - **Actions out:** callbacks for user actions (start/stop/reset measurement,
    set brightness, set overlay, select preset, …). The app turns them into UART commands
    (ADR 0004); the simulator logs them and applies them to its simulated state.

### PC simulator

- `dash/sim/` builds the same `ui/` code natively on macOS with SDL2
  (`brew install sdl2`), based on the official LVGL PC port. Window size 480×800,
  the mouse acts as touch input.
- A simulated data source plays back test rides (speed profile, distances), so
  animations and the distance measurement can be checked without hardware.
- Headless mode: the simulator can render without a window and write screenshots
  (PNG) of each screen. This allows automated checks and reviewing GUI changes
  without the board.
- LVGL configuration (`lv_conf.h`, fonts, colour depth) is shared between
  firmware and simulator, so the preview matches the device.

### Display and fonts

- Portrait orientation, 480×800, RGB565.
- Dark theme for readability at night.
- One typeface (Montserrat, SIL OFL, `ui/assets/`). Text sizes with icons use the
  LVGL built-in Montserrat fonts (ASCII and `LV_SYMBOL_*`) with a Tiny TTF fallback
  of the same size for umlauts; large number sizes are rendered by Tiny TTF from
  the embedded TTF. No generated font files are needed.
- Numbers use a **fixed decimal comma**: the integer part is right-aligned to a
  fixed position, decimals follow; units are smaller and grey on the same
  baseline. Digits of different width therefore never make the comma jump.
- The simulator window has a fixed size (resizing would change the display
  resolution); `--zoom` scales it.

### Screens

Three screens, switched by swiping or by tabs (icon above text) at the bottom:

1. **Ride**
   - Speed in km/h inside an arc gauge (0 … 16 km/h) with a speedometer scale
     (ticks at every km/h, numbers at 0, 5, 10, 15); the decimal is shown at
     60 % size in grey so it does not distract
   - Trip and total distance directly below the gauge
   - Two distance measurements (ADR 0008), each with start/stop and reset
   - Link state and battery voltage; "--" when no valid frame for 500 ms
     (ADR 0004)
2. **LED**
   - Global brightness slider
   - Pattern list (built-in and user patterns from core, ADR 0011, 0013)
   - Overlay list (presets, ADR 0012) and sliders for the active overlay preset:
     level at start, middle and end, fade length at start and end. Changes go
     into the preset; a built-in preset is copied first (ADR 0017)
   - Button to enable the preset editor via Wi-Fi (ADR 0013), showing SSID,
     password and URL as text and QR code

3. **Settings**
   - Display brightness (dash backlight, 5 … 100 %, stored on dash)
   - Resolution of the distance measurement display (ADR 0008)
   - Backward travel in distance measurements: subtract or add (ADR 0015)
   - Encoder calibration by test ride, with history (ADR 0014)
   - Date and time (manual entry, ADR 0014)

Further content (e.g. diagnostics such as the NVS write counter) can be added
without changing this structure.

## Consequences

- GUI changes can be previewed on the Mac in seconds; flashing is only needed for
  hardware-related work.
- Screenshots from the simulator make GUI changes reviewable (also by tools).
- The GUI must not call ESP-IDF APIs; this keeps it testable but requires the
  model/action interface.
- Fonts and images are compiled in as C arrays (LVGL font converter); large fonts
  for the speed display cost flash, which is plentiful (16 MB).
- LVGL frame buffers live in the dash PSRAM (32 MB, see architecture.md).
