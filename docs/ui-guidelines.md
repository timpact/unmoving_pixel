# dash UI guidelines

Normative rules for the dash GUI (ADR 0010). Every GUI change must follow them;
the automated checks of ADR 0016 verify the measurable ones. When feedback reveals
a new rule, it is added here (and, if measurable, as a check).

Rules are numbered so that checks and reviews can refer to them (e.g. `N2`).

## Display and layout (L)

- **L1** Portrait, 480×800 px, RGB565. Physical size ~56×93 mm → **~8.6 px/mm**.
- **L2** Tab bar at the bottom, 72 px high, icon above text. Three tabs: Fahrt,
  LED, Einstellungen.
- **L3** The ride screen must fit without scrolling. Other screens may scroll
  vertically. No screen scrolls horizontally.
- **L4** Page padding 16 px; gap between cards 10–12 px; card padding 16 px
  (compact cards on the ride screen: 12 px).
- **L5** Radius: cards 16 px, buttons 14 px.
- **L6** No element may extend beyond the screen or be hidden behind the tab bar
  (except content of scrollable pages).

## Typography (T)

- **T1** One typeface: Montserrat Medium. Fixed size steps only:

  | Role | Size |
  |---|---|
  | captions, units, table text, gauge numbers | 18 px |
  | labels, buttons, dropdowns | 22 px |
  | distances, dialog values | 34 px |
  | measurement values | 42 px |
  | speed, decimal part | 65 px (60 % of the integer part) |
  | speed, integer part | 108 px |

- **T2** Text is never cut off. Text wraps only where wrapping is intended
  (hints, explanations, dialog messages); labels, values, buttons, table cells
  and tab names are single-line.
- **T3** Button labels are short (one or two words). Explanations belong in a
  text above the buttons, not on the button.
- **T4** Language: German. Umlauts and ß must render correctly.

## Numbers and units (N)

- **N1** German format: thousands separator `.`, decimal separator `,`
  (e.g. `1.234,56`). Percentages with a space: `80 %`.
- **N2** **Fixed decimal comma:** the integer part is right-aligned to a fixed
  position. The comma never moves, whatever digits are shown.
- **N3** **Fixed unit position:** the unit sits at a fixed position after room
  for the widest digit in every decimal place that can occur. The unit never
  moves.
- **N4** Units are shown in the caption size (18 px), grey (`muted`), on the
  same baseline as the number.
- **N5** Room before the comma covers the largest value that can occur,
  including a minus sign where values can be negative (e.g. measurements:
  `-99.999`).
- **N6** Speed: decimal place at 60 % size in grey; unit line below the number.
  Backward travel: magnitude plus "rückwärts" (ADR 0015).
- **N9** Speed gauge: 270° arc from 0 to 16 km/h (diameter 244 px), filled up to
  the current speed. Scale outside the arc: a tick at every km/h (`border`),
  longer ticks and numbers at 0, 5, 10 and 15 (`muted`, 18 px). The numbers are
  label objects, so the checks see them (no overlap with the speed).
- **N7** Live values are updated at most 20 times per second and only redrawn
  when the text changes. A changing value must never move any other element
  (no layout shift).
- **N8** Dates: `dd.mm.yyyy hh:mm`; in tables `dd.mm.yy hh:mm`. Unknown date:
  `unbekannt`.

## Colours (C)

Dark theme. Colours have a fixed meaning:

| Token | Hex | Meaning |
|---|---|---|
| `bg` | `#0d1117` | screen background, input fields |
| `card` | `#161b22` | cards, tab bar |
| `border` | `#30363d` | borders, separators, inactive arc |
| `text` | `#e6edf3` | primary text and values |
| `muted` | `#8b949e` | captions, units, secondary text, neutral buttons |
| `accent` | `#2dd4bf` | primary actions, active indicators, sliders |
| `ok` | `#22c55e` | start, running, valid results, connected |
| `warn` | `#f59e0b` | stop/pause, warnings, confirmation step |
| `danger` | `#ef4444` | errors, no connection, rejected input |

- **C1** Only these tokens are used. Text on coloured buttons uses `bg`.
- **C2** Disabled controls: 30 % opacity.

## Touch targets (I)

- **I1** Every touch target is at least **60×60 px** (~7 mm), including an
  extended click area where the visible element is smaller (sliders, switches).
- **I2** Controls used while riding (ride screen) are at least **64 px** high.
- **I3** At least 10 px between neighbouring touch targets.

## States and feedback (S)

- **S1** No valid frame from core for 500 ms: all values coming from core show
  `--` or are hidden (speed, distances, battery); the status line shows
  "keine Verbindung" in `danger` (ADR 0004).
- **S2** Every user action gives visible feedback within 100 ms (pressed state,
  value change, dialog).
- **S3** States that are defined by core (e.g. calibration running) are shown
  from the reported state, not from a local assumption, so that the display is
  correct after a dash restart.

## Dialogs (D)

- **D1** Dialogs lie on the top layer, dim the background (80 % black) and block
  touches to the page below.
- **D2** Primary action left, secondary action (cancel, discard) right; both
  equally wide.
- **D3** Unusual or risky actions (e.g. calibration deviation > 10 %) show an
  explicit warning text first and require a second tap on a button labelled
  "Bestätigen".
- **D4** Numeric input uses a keypad with only the keys needed (digits,
  backspace, OK).
