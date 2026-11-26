# ADR 0016: GUI guidelines and automated GUI checks

- Status: Accepted
- Date: 2026-11-26

## Context

The dash GUI is developed in the PC simulator (ADR 0010). Until now, GUI changes
were checked by looking at single screenshots, against an implicit understanding
of the requirements. This missed problems:

- The decimal comma and the units jumped with changing digits. A single
  screenshot cannot show this; it only appears between frames.
- A button label was cut off ("rklich übernehme") in a dialog state that was not
  part of the screenshots.
- After a change, only the places in focus were checked; regressions elsewhere
  would go unnoticed.

The rules that emerged from the rider's feedback were not written down, so every
extension risked repeating the same mistakes.

## Decision

### 1. Written guidelines

`docs/ui-guidelines.md` is the normative rule set for the GUI (layout, typography,
numbers and units, colours, touch targets, states, dialogs). Rules are numbered
(e.g. `N2`, `I1`). New rules from feedback are added there.

The guidelines are a living document rather than an ADR: they grow with the GUI,
while this ADR records the decision to have them and how they are checked.

### 2. Automated checks in the simulator

The simulator gets a check mode:

```
dash_sim --check [--update-references]
```

It runs headless, renders defined scenarios and exits with a non-zero code if a
check fails. It takes a few seconds.

| Check | What it verifies | Rules |
|---|---|---|
| **Layout stability** | For every fixed-comma number, a sweep of values (0 … maximum, negative values, all digits, link lost) is rendered; the x positions of comma and unit must not change. All other objects of the screen must not move either. | N2, N3, N7 |
| **Text fits** | Every label lies completely inside the visible area of its clipping parent; single-line labels must not wrap. Checked on every scenario. | T2, L6 |
| **Touch targets** | Every clickable object (including its extended click area) is at least 60×60 px, on the ride screen at least 64 px high; neighbouring targets keep 10 px distance. | I1–I3 |
| **Colour tokens** | Text and background colours of all objects belong to the token list. | C1 |
| **Reference screenshots** | Every scenario is compared pixel by pixel with an approved reference image. Differences are reported with a diff image. | all visual rules |

Rendering in the simulator is deterministic (same LVGL version, fixed simulated
time, fixed test data), so references can be compared exactly.

### 3. Scenarios

The scenario list covers all screens and states, at least:

- Ride: normal ride, link lost, backward travel, negative measurement, maximum
  values (e.g. measurement 99.999,99 m, speed above the 16 km/h arc range),
  both measurements running and stopped
- LED: top and bottom of the page, editor on, user overlay active, built-in
  overlay with "no free slot" notice (ADR 0017)
- Settings: default, after calibration, empty history, full history (10 entries)
- Calibration dialog: counting, empty input, too short, negative net distance,
  valid result, warning, confirmation step
- Date/time dialog

New screens or states get a scenario when they are added.

### 4. Reference images

- Stored as PNG in `dash/sim/reference/`, so they can be viewed on GitHub and in
  the IDE. The simulator writes and reads them with a small PNG module on top of
  zlib (part of macOS and Linux), so no third-party code is needed.
- `--update-references` writes new references. They are only updated after the
  rider has approved the change; the commit then contains code and new
  references together, so the visual change is visible in the diff.

**Introduced later:** while the GUI still changes in almost every iteration, the
reference comparison is postponed. Every intended change would require new
references (~450 KB per full set, kept forever in the Git history), and the
known problem classes are already covered by the rule checks, which need no
references. As long as `dash/sim/reference/` does not exist, the comparison is
skipped (not reported as a failure); `--check` still writes all screenshots for
review. References are created once the GUI is stable, at the latest before it
moves to the real display or before larger changes elsewhere in the GUI.

### 5. Workflow

- After every GUI change the checks run before screenshots are shown to the
  rider.
- A failing rule check is fixed before review; a failing reference comparison
  (once references exist) is either a regression (fix) or an intended change
  (show it, then update the references).
- Feedback that reveals a new rule → add to the guidelines → add a check if it is
  measurable.

## Consequences

- Known problem classes (moving digits and units, cut-off text, too small touch
  targets) are found automatically and do not come back.
- Every visual change is explicit: it shows up as a reference image change in the
  commit.
- The checks only cover what the simulator renders; the real panel (colours,
  brightness in sunlight, touch accuracy) still needs a check on the device.
- Reference images add some size to the repository (PNG, roughly 20–60 KB per
  scenario).
- Some existing elements will initially fail the touch-target check (sliders are
  14 px high, the knob is smaller than 60 px); they get an extended click area.
- The checks can later run in CI (GitHub Actions) without changes.
