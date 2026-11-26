# ADR 0019: dash firmware – board support and structure

- Status: Accepted
- Date: 2026-11-26

## Context

The dash GUI exists as LVGL code in `dash/ui` and runs in the PC simulator
(ADR 0010). The UART protocol is implemented in `common/` (ADR 0004). The real
dash firmware on the Guition JC4880P443 has to bring both together: display,
touch, backlight, UART to core, settings in NVS.

The board has no public schematic. Display parameters and pins were taken from
the ESPHome model `JC4880P443` (`esphome/components/mipi_dsi/models/guition.py`)
and two working ESPHome device configurations, and **verified on the board** with
a bring-up firmware (`dash/main/board.c`): test image with correct colours and
orientation, touch points exactly under the finger.

## Decision

### Board support (verified)

| Function | Details |
|---|---|
| Display | ST7701, MIPI DSI, 2 data lanes, 500 Mbit/s per lane, 480 × 800 portrait, RGB565 |
| Video timing | DPI clock 34 MHz; hsync pulse 12, back porch 42, front porch 42; vsync pulse 2, back porch 8, front porch 166 |
| Panel init | vendor sequence of the ESPHome model, then COLMOD 0x55, MADCTL 0x00, INVOFF, SLPOUT (120 ms), DISPON |
| DSI PHY supply | internal LDO channel 3 at 2.5 V |
| Display reset | GPIO5 |
| Backlight | GPIO23, LEDC PWM (1 kHz, 10 bit) |
| Touch | GT911 at I2C address 0x5D; I2C SDA GPIO7, SCL GPIO8, 400 kHz; reset GPIO3; polled (interrupt line not used) |
| Other I2C devices | 0x14, 0x18 (probably the audio codec; not used) |
| UART to core | UART1, GPIO33 RX, GPIO31 TX (ADR 0006) |
| Wi-Fi co-processor | ESP32-C6 over SDIO 4-bit, 40 MHz: CLK 18, CMD 19, D0–D3 14–17, reset 54 (verified 2026-11-26 with `dash/test/c6_check`). Factory firmware ESP-Hosted 2.3.2, updated 2026-11-26 by OTA from the P4 to **2.13.1** (same version on host and C6; host: `espressif/esp_hosted` ~2.13.1 with `espressif/esp_wifi_remote` 1.6.3, the last version that excludes ESP-Hosted 3.x; ADR 0013) |

The board support uses only ESP-IDF drivers (no BSP or panel components) and
lives in `dash/main/board.c`.

### Firmware structure

```
dash/main/
├── board.c/.h      display, backlight, touch (this ADR)
├── lvgl_port.c/.h  LVGL display and input driver on top of board.c
├── link.c/.h       UART1 + up_proto (common/): STATUS/SETTINGS → model, commands with seq/ACK/retry
├── settings.c/.h   dash NVS storage for dash/app/dash_state (see "NVS on the dash")
└── main.c          start-up, model update loop
```

- **LVGL** 9.3.0 (same version as the simulator) as ESP-IDF component from the
  component registry (`lvgl/lvgl`), pinned in `dash/main/idf_component.yml`;
  `dependencies.lock` is committed. The same `dash/ui/lv_conf.h` is used on the
  device and in the simulator.
- **Rendering** (measured with continuous scrolling of the settings page, the
  most demanding case; the first version scrolled visibly jerky):

  | Step | Render | Frame | fps |
  |---|---|---|---|
  | partial buffers 80 lines, copy with memcpy, code from flash (40 MHz DIO) | 38 ms | 57 ms | 14 |
  | code and constants (fonts) executed from PSRAM (`CONFIG_SPIRAM_XIP_FROM_PSRAM`) | 21 ms | 38 ms | 20 |
  | copy into the DPI frame buffer with the 2D-DMA, asynchronously | 20 ms | 29 ms | 26 |
  | partial buffers 160 lines | 14 ms | 22 ms | 32 |
  | direct mode in two DPI frame buffers, switch at frame end, L2 cache 256 KB / 128 B lines | 17–25 ms | 33 ms | **30** |

  The partial buffers (one DPI frame buffer) showed clearly visible tearing on
  the device, which Espressif states for single-buffer setups. Decision:
  - LVGL draws in direct mode into the DPI frame buffer that is not shown (two
    frame buffers in PSRAM, `num_fbs = 2`). After the last area of a frame the
    panel switches to it at the next frame end (`esp_lcd_panel_draw_bitmap` with
    the buffer pointer); `on_refresh_done` then releases LVGL. No tearing.
  - Because a full redraw takes a bit more than one frame (16.7 ms), scrolling
    runs at a steady 30 fps, which is Espressif's typical target (≥ 30 fps).
    Unchanged screens are not redrawn (rule P1, `docs/ui-guidelines.md`).
  - Code and constants run from PSRAM, L2 cache 256 KB with 128-byte lines,
    two LVGL software draw units (`LV_OS_FREERTOS`). No draw buffers in internal
    RAM any more (~225 KB free for Wi-Fi, ADR 0013).
  - LVGL ignores its Kconfig options (`LV_KCONFIG_IGNORE`), so device and
    simulator use exactly the same `lv_conf.h`.
  - LVGL 9.3 because the simulator already used it (same version on device and
    simulator); an update to 9.4 or later is possible at any time (component
    version, simulator, `dash_sim --check`, new `dependencies.lock`).
  - Not used: the pixel accelerator (PPA, LVGL ≥ 9.4). It only fills rectangles
    and copies, scales, rotates and blends images. Our render time goes mostly
    into anti-aliased text from the TTF font, rounded corners, borders and the
    chart lines, which stay in software; each PPA job also costs cache sync and
    setup, so small areas are often faster on the CPU. Espressif states that it
    usually gives no noticeable gain. Expected: a few ms per frame, not enough
    for 60 fps (not measured).
  - Not used: three frame buffers. LVGL could draw on without waiting for the
    frame end (~40–50 fps on average), but frames would be shown alternately for
    one or two display frames (uneven, visible as micro-judder while scrolling),
    with a little more latency and own switching logic (LVGL direct mode supports
    two buffers). A steady 30 fps is preferred.
  - Possible improvement: 60 fps needs a full redraw in under ~16 ms (display
    refresh ~60 Hz), then two frame buffers are enough. First measure where the
    17–25 ms go (text, rounded corners, borders, chart), e.g. with a profiler in
    the simulator, then reduce specifically: fewer or smaller corner radii, no
    shadows, a cached chart on the LED page.

  Sources: [esp_lvgl_adapter](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/tools/esp_lvgl_adapter.html),
  [LVGL tips for Espressif](https://docs.lvgl.io/9.5/integration/chip_vendors/espressif/tips_and_tricks.html),
  [Espressif GUI optimisation](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/lcd-application-note/gui-optimization-summary.html),
  [Espressif LCD FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html),
  [LVGL PPA](https://docs.lvgl.io/9.4/details/integration/chip_vendors/espressif/hardware_accelerator_ppa.html).
- **Checklist for smooth scrolling on the P4** (summary of the above):
  1. Build: `CONFIG_COMPILER_OPTIMIZATION_PERF`, `CONFIG_FREERTOS_HZ=1000`,
     PSRAM at 200 MHz, code and constants from PSRAM
     (`CONFIG_SPIRAM_XIP_FROM_PSRAM`), L2 cache 256 KB with 128-byte lines.
  2. Display: two DPI frame buffers, LVGL direct mode, switch at frame end
     (no tearing); two software draw units with `LV_OS_FREERTOS`.
  3. GUI: only the page scrolls, rows and cards are not scrollable (I4); hard
     stop at the page ends so the page follows the finger (I5); texts, flags
     and styles set only on a real change, otherwise the 20 Hz model update
     redraws (P1).
  4. Measure with finger swipes (real or synthetic touch input, `DASH_BENCH_SCROLL`)
     and the LVGL sysmon (`DASH_PERF_LOG`). Programmatic scrolling with
     animations redraws only part of the time and gave too optimistic numbers.
- **GUI checks:** `dash_sim --check` also tests swiping from sliders (I4), the
  hard stop at both page ends (I5) and that a model update with unchanged values
  does not invalidate anything (P1).
- **LVGL 9.3 workaround:** without elastic scrolling LVGL limits the scroll only
  at the top end; at the bottom end (`elastic_diff`, wrong sign for negative
  steps) the page runs beyond it and springs back. A scroll event handler on each
  page pulls it back at once (`ui.c`). Check again when updating LVGL.
- **Development aids:** `DASH_BENCH_SCROLL` (menuconfig) scrolls a page
  (`DASH_BENCH_TAB`) with synthetic finger swipes for 10 s at start and logs the
  frame rate; `DASH_BENCH_MEASURE` first starts, stops and resets a measurement.
  `DASH_PERF_LOG` logs the LVGL sysmon (fps, render and flush time) every second.
- **Tasks:** one GUI task (LVGL timer handler, model update at 20 Hz, touch polled
  every 20 ms by the LVGL input driver) and one link task (UART receive, parser,
  command queue with ACK and up to 3 retries after 100 ms, ADR 0004). The model
  is exchanged between the two tasks under a mutex.
- **Demo mode:** until core speaks the protocol, the dash can run with the
  simulated core of the PC simulator (`dash/sim/sim_core.c`, platform-independent
  C). It is selected in `menuconfig` (`DASH_DEMO_CORE`). This puts the complete
  GUI on the real display early, and the same test ride as in the simulator can
  be looked at on the device.
- **Link loss:** no valid frame for 500 ms → `link_ok = false`, the GUI shows "--"
  (ADR 0004).
- **NVS on the dash** (added 2026-11-26): only the dash settings, written 2 s
  after the last change (like core):

  | Key | Value | Default |
  |---|---|---|
  | `disp_bright` | display brightness, 5 … 100 % | 70 |
  | `meas_dec` | measurement resolution, 0 … 2 decimals of a metre | 2 (1 cm) |
  | `meas_add_bw` | backward travel added (1) or subtracted (0), ADR 0015 | 0 |
  | `wifi_pw` | Wi-Fi password, generated once (ADR 0013) | comes with the editor |

  - Namespace `dash`, one key per value. A missing key or a value out of range
    gives the default, so settings can be added later without a format version.
  - Not stored: the distance measurements (RAM only, ADR 0008), the ride panel
    "Messung | Licht" (starts with "Licht", ADR 0018), the switching states of
    the lights (ADR 0018) and the time (no RTC, ADR 0014).
  - Code: the dash-owned state (settings, measurements, ride panel) lives in
    `dash/app/dash_state.c`, platform-independent, with the defaults and value
    ranges. Loading and saving go through two functions passed in by the caller:
    NVS on the device (`dash/main/settings.c`), RAM in the PC simulator. The
    demo core (`sim_core.c`) then only provides what core will send over UART.
  - The NVS partition keeps the position and size of the former interim table
    (0x9000, 24 KB), so the settings survived the change of the table.
- **Partition table** (added 2026-11-26, `dash/partitions.csv`, 16 MB flash):

  | Partition | Offset | Size | Purpose |
  |---|---|---|---|
  | `nvs` | 0x9000 | 24 KB | dash settings, later the Wi-Fi password |
  | `otadata` | 0xF000 | 8 KB | selects the app slot to boot |
  | `ota_0` | 0x20000 | 4 MB | dash firmware |
  | `ota_1` | 0x420000 | 4 MB | second slot for firmware updates |
  | `c6fw` | 0x820000 | 2 MB | C6 image for C6 updates (`dash/test/c6_check`) |
  | `coredump` | 0xA20000 | 64 KB | crash report, `idf.py coredump-info` |
  | free | 0xA30000 | ~5.8 MB | reserve |

  - Two app slots although the firmware is flashed over USB for now: a later
    update from the browser (with fallback to the old firmware) needs no new
    table. The app is ~1 MB today, ~1.5–1.8 MB expected with Wi-Fi and HTTP.
  - No `phy_init`: the P4 has no radio, the C6 does Wi-Fi.
  - No partition for the web editor: it is embedded in the firmware (ADR 0013).

### Order of work

1. Board support (done, verified)
2. LVGL port with the GUI in demo mode
3. Settings in NVS
4. Wi-Fi editor (ADR 0013): C6 firmware check and update (done), dash partition
   table (done), then access point, HTTP server and the editor. Until the UART link exists, the editor works against the demo core
   (presets kept in dash RAM), like the mock dash of the dev server.
5. UART link with the protocol (testable against a minimal protocol responder on
   the S3 before the real core firmware exists)

## Consequences

- The GUI code is identical in the simulator and on the device; differences can
  only come from the port (rendering, touch, timing).
- No dependency on board support packages; the board parameters are documented
  here and in `board.c`.
- The component manager downloads LVGL once at the first build (network needed
  then); the version is pinned.
- Changing the partition table later should keep `nvs` at 0x9000 with 24 KB,
  otherwise the settings fall back to the defaults once.
- Demo mode allows GUI work on the device without core.
