# dash simulator

Runs the dash GUI (`dash/ui`) on macOS in a 480×800 window (ADR 0010).
The mouse acts as touch input. A simulated core plays back a test ride
(accelerate, cruise at ~12.6 km/h, brake, stand – repeats every 60 s).

## Requirements

```sh
brew install sdl2 cmake ninja pkg-config
```

LVGL 9.3.0 is downloaded automatically by CMake on the first build.

## Build and run

```sh
cmake -S dash/sim -B dash/sim/build -G Ninja
cmake --build dash/sim/build
dash/sim/build/dash_sim
```

The window has a fixed size: resizing would change the display resolution and
re-layout the GUI instead of scaling it. Use `--zoom` to make it smaller or larger.

User actions (buttons, sliders, lists) are printed to the terminal as
`[action] …`, as they would be sent to core.

## Options

| Option | Effect |
|---|---|
| `--zoom <f>` | scales the window (e.g. `0.6`); the GUI keeps its 480×800 layout |
| `--link-drops` | simulates a lost core link for 2 s every 30 s ("--" display) |
| `--screenshot <dir>` | headless: no window, writes `ride.bmp`, `led_top.bmp`, `led_bottom.bmp`, `settings.bmp`, `calib_counting.bmp`, `calib_input.bmp`, `calib_warn.bmp`, `calib_confirm.bmp`, `settings_after_calib.bmp` |
| `--time <s>` | with `--screenshot`: simulated seconds before the screenshots (default 20) |
| `--measure` | with `--screenshot`: starts measurement 1 at t = 5 s and measurement 2 at t = 12 s |

Convert screenshots to PNG on macOS: `sips -s format png ride.bmp --out ride.png`.
