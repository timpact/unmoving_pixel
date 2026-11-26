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
| `--overlays-full` | all user overlay slots taken: moving an overlay slider on a built-in overlay shows "Kein freier Platz" |
| `--screenshot <dir>` | headless: no window, writes PNG screenshots of a simulated ride (ride, LED, settings, calibration steps) |
| `--time <s>` | with `--screenshot`: simulated seconds before the screenshots (default 20) |
| `--measure` | with `--screenshot`: starts measurement 1 at t = 5 s and measurement 2 at t = 12 s |

## GUI checks (ADR 0016)

```sh
dash/sim/build/dash_sim --check
```

Renders all scenarios with fixed test data and checks the rules of
[`docs/ui-guidelines.md`](../../docs/ui-guidelines.md): fixed comma and units
(value sweep), text fits, ride screen without scrolling, touch target size and
spacing, colour tokens, and a pixel comparison with the approved reference
screenshots in `reference/`. Exit code 0 = all checks passed.

The reference comparison is postponed until the GUI is stable: as long as
`dash/sim/reference/` does not exist, it is skipped (ADR 0016).

- Results and diff images (`*_diff.png`, differences in red) are written to
  `dash/sim/build/check/`.
- Run the checks after every GUI change.
- After an intended visual change has been reviewed and approved:
  `dash_sim --check --update-references`, then commit code and references together.
