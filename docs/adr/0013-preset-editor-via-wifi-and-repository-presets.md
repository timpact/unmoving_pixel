# ADR 0013: Preset editor via Wi-Fi on dash, presets in the repository

- Status: Accepted
- Date: 2026-11-25 (updated 2026-11-25: overlays as second preset category,
  presets in the repository; updated 2026-11-26: Wi-Fi, serving and
  implementation details)

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
- Details (added 2026-11-26):

  | Item | Decision |
  |---|---|
  | SSID | fixed `unmoving-pixel` |
  | Password | 12 characters in groups of three, e.g. `k7q-29v-mn4-x8d`: lowercase letters and digits without 0/o and 1/l (~60 bit), from the hardware random number generator |
  | Storage | NVS key `wifi_pw` (ADR 0019), generated when Wi-Fi is switched on the first time; "Neues Passwort" in the editor area of the LED screen generates a new one and restarts Wi-Fi |
  | Security | WPA2/WPA3 mixed mode |
  | Address | `http://192.168.4.1`, no mDNS (`.local` often fails on Android) |
  | Channel, clients | channel 6, at most 2 clients (e.g. phone and laptop) |

  - The QR code joins the network (`WIFI:T:WPA;S:…;P:…;;`); SSID, password and
    URL are shown as text below it. No second QR code for the URL.
  - **No captive portal:** iOS would open the editor in its captive portal
    sheet, where export downloads do not work and local storage is lost. The
    URL is typed once in the normal browser instead.
  - **Auto-off:** Wi-Fi switches off 10 min after the last HTTP request; every
    request counts, including the polling of `/api/state` (ADR 0017). As long as
    the editor is open in a browser, Wi-Fi stays on; whether a background tab
    keeps polling depends on the browser. The editor area shows the number of
    connected clients and, during the last 2 min, the time left.
  - **C6:** ESP-Hosted is initialised in the background at dash start (the GUI
    does not wait; the C6 is reset, Wi-Fi stays off). Switching off stops Wi-Fi;
    the C6 stays powered. Holding it in reset would save some more current; to
    be measured once the power supply is defined.

### Web editor

- A single-page web app (HTML/CSS/JavaScript, no build-time framework required),
  stored gzip-compressed in the dash firmware (embedded files, ~35 KB) and
  served by `esp_http_server` (updated 2026-11-26: no separate flash partition;
  editor and dash API always match, one flash step, no file system; ADR 0019).
- The same web app lives in the repository (`tools/preset-editor/`) and can be
  opened locally on the desktop without the scooter; it then works on files
  (import/export) only.
- **Serving** (added 2026-11-26): a CMake step gzips the editor files (HTML, JS,
  CSS and `mock-dash.mjs`, which the editor also imports on the dash) and embeds
  them in the firmware. They are served with `Content-Encoding: gzip` and an
  ETag from the firmware build, so a browser never keeps an old editor after a
  firmware update. `presets/index.json` is not served (only the local mode on
  the desktop uses it).
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

### Implementation on dash (added 2026-11-26)

- `dash/app/editor_api.c`, platform-independent: request parsing, validation
  and response JSON (cJSON), with a unit test whose cases are taken from the
  Node tests of the mock dash (`tools/preset-editor/`), so both follow the same
  rules: limits, `409 Conflict`, built-in slots read-only (`403`), German error
  messages. `dash/main/wifi_ap.c` and `dash/main/http_server.c` connect it to
  ESP-IDF (`esp_wifi_remote`, `esp_http_server`).
- Request bodies up to 256 KB (a pattern with 2048 segments is ~120 KB of JSON);
  the buffer is in PSRAM.
- The HTTP server runs in its own task and accesses data shared with the GUI
  only under a lock. Actions such as activating a preset go through the same
  `ui_actions` as the touch GUI, so GUI and editor always agree.
- **Until the UART link exists** (ADR 0019): the demo core keeps the presets in
  dash RAM, like the mock dash; user presets are lost on restart, built-in
  presets remain. The built-in presets come from the same `presets/*.json`: a
  build script converts them into C data, the same script that will generate
  the built-in presets of core later.

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
