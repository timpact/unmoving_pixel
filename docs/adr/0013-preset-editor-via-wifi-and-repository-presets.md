# ADR 0013: Preset editor via Wi-Fi on dash, presets in the repository

- Status: Accepted
- Date: 2026-11-25 (updated 2026-11-25: overlays as second preset category,
  presets in the repository)

## Context

There are two kinds of presets:

- **Patterns** (ground-fixed, ADR 0011): can be long and detailed.
- **Overlays** (strip-fixed, ADR 0012): a few parameters.

Editing patterns on the 4.3" touch display is tedious; a large screen (laptop,
tablet, phone) is much better suited. Presets must end up on core, which stores
them (single source of truth). The dash must offer the stored presets in a list.
Presets should also be kept in the repository (versioned, shareable, built into
the firmware).

Options considered for getting presets onto the scooter:

1. Browser editor + USB connection to core (Web Serial)
2. Browser editor + USB connection to dash, forwarded over UART
3. **Editor served by dash over Wi-Fi** (ESP32-C6 co-processor)
4. Preset files on the dash SD card

Option 3 was chosen: no cable, no SD card swapping, works from any device with a
browser, and the dash is easy to reach on the handlebar.

## Decision

### Presets in the repository

```
presets/
├── patterns/     # one JSON file per pattern, e.g. stripes-red.json
└── overlays/     # one JSON file per overlay, e.g. soft-edges.json
```

- The repository is the library of presets. Files are plain JSON (format below),
  so they can be reviewed, diffed and shared.
- File names start with a two-digit number (`00-none.json`, `01-soft-edges.json`);
  the built-in slots are assigned in file name order, so slot numbers stay stable
  when files are added at the end.
- **Built-in presets:** at build time, a script converts all files in `presets/`
  into C arrays that are compiled into the core firmware as read-only slots.
- **User presets** are created in the editor and stored on core. They get into
  the repository by exporting them from the editor (single file or all as a
  bundle) and committing the files. Importing JSON files into the editor works the
  other way round.

### Lists on the dash

- core provides the list of all presets per category (built-in and user slots,
  name, length for patterns), see ADR 0004.
- The LED screen of the dash shows two lists: patterns and overlays. Selecting an
  entry activates it.
- The list is refreshed whenever core reports a changed `preset_list_rev`.

### Wi-Fi

- The dash uses its ESP32-C6 as Wi-Fi co-processor (ESP-Hosted over SDIO,
  `esp_wifi_remote` on the P4).
- **Access point mode:** dash opens its own network (SSID e.g. `unmoving-pixel`,
  WPA2). No home network or internet needed; works anywhere.
- **On demand only:** Wi-Fi is off by default. It is switched on from the LED
  screen ("Editor") and switches off automatically after 10 min without editor
  activity, or when switched off on the screen.
- The dash shows SSID, password and URL as text and as QR code (LVGL QR widget).
- The password is generated once, stored in dash NVS, and can be regenerated on
  the dash.

### Web editor

- A single-page web app (HTML/CSS/JavaScript, no build-time framework required),
  stored gzip-compressed in the dash firmware (embedded files, ~35 KB) and
  served by `esp_http_server` (updated 2026-11-26: no separate flash partition;
  editor and dash API always match, one flash step, no file system; ADR 0019).
- The same web app lives in the repository (`tools/preset-editor/`) and can be
  opened locally on the desktop without the scooter; it then works on files
  (import/export) only.
- Features:
  - **Patterns:** segment list with length (cm), colour and brightness; ground
    view of the whole pattern
  - **Overlays:** levels (start, middle, end) and fade lengths, with a curve plot
  - **Preview:** animated strip with pattern and overlay combined (simulated ride
    at adjustable speed, LED pitch and strip length as on the scooter)
  - "Try on strip": sends a pattern to the core preview slot (RAM only), or an
    overlay as active overlay without saving
  - "Save to slot", list, load, rename, delete
  - Export (single preset or all as bundle) and import of JSON files

### HTTP API on dash

`{category}` is `patterns` or `overlays`.

| Method | Path | Function |
|---|---|---|
| GET | `/api/{category}` | list of slots (built-in and user) |
| GET | `/api/{category}/{slot}` | one preset as JSON |
| GET | `/api/state` | settings and preset lists with revisions; polled every second by the editor (ADR 0017) |
| PUT | `/api/{category}/{slot}` | store a preset (user slots only); carries the base revision, answers `409 Conflict` if the preset changed meanwhile (ADR 0017) |
| DELETE | `/api/{category}/{slot}` | delete a user preset |
| POST | `/api/preview/{category}` | try a preset on the strip without storing it |
| GET | `/api/export` | all user presets as one JSON bundle |
| GET | `/api/settings` | current LED settings (ADR 0012) |
| POST | `/api/time` | device time from the browser, sent automatically when the editor opens (ADR 0014) |

### JSON formats

Pattern (mirrors ADR 0011, brightness in %):

```json
{
  "format_version": 1,
  "type": "pattern",
  "name": "Stripes",
  "segments": [
    { "length_cm": 4, "color": "#ff0000", "brightness": 100 },
    { "length_cm": 4, "color": "#0040ff", "brightness": 60 },
    { "length_cm": 2, "color": "#000000", "brightness": 0 }
  ]
}
```

Overlay (mirrors ADR 0012):

```json
{
  "format_version": 1,
  "type": "overlay",
  "overlay_type": "edge_fade",
  "name": "Soft edges",
  "level_start": 20,
  "level_mid": 100,
  "level_end": 20,
  "fade_start_mm": 150,
  "fade_end_mm": 150
}
```

Bundle (export of all user presets, import of several presets at once):

```json
{ "format_version": 1, "type": "bundle", "presets": [ { "type": "pattern", … }, { "type": "overlay", … } ] }
```

### Data flow

```
browser ──HTTP──► dash ──UART (ADR 0004)──► core ──► cfg NVS
                  validates JSON,            validates, stores,
                  converts to binary         ACKs with CRC
```

- dash validates the JSON (limits from ADR 0011/0012), converts it to the binary
  format and transfers it to core (ADR 0004). The HTTP request only succeeds after
  core has confirmed the stored preset (CRC match).
- dash does not keep its own copy of the presets; it asks core.

### Security

- WPA2 access point with a random password; HTTP without TLS inside this local
  network (acceptable for this use; no internet connection).
- Wi-Fi is only on when explicitly enabled.

## Consequences

- Comfortable editing on any device with a browser, without cables.
- Presets are versioned in the repository; the built-in presets always match the
  files in `presets/`.
- Presets can be designed offline on the desktop and imported later.
- core remains the only place where presets are stored on the scooter.
- Getting user presets into the repository is a manual step (export, commit).
- More work on the dash: ESP-Hosted, access point, HTTP server, web app,
  JSON validation, transfer over UART.
- The C6 must run an ESP-Hosted firmware version compatible with the ESP-IDF
  version on the P4. Done 2026-11-26: the factory firmware 2.3.2 was updated by
  OTA from the P4 to 2.13.1, the version used on the host
  (`dash/test/c6_check`, ADR 0019). Recovery if an update fails: the C6 UART0
  pins (ADR 0006). Host and C6 versions are changed together only.
- Extra current draw while Wi-Fi is on (on demand only).
- Every change of the web app needs a dash firmware build (both live in the
  same repository).
