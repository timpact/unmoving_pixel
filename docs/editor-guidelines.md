# Preset editor UI guidelines

Rules for the web-based preset editor (ADR 0013, 0017). The editor is used mainly
in a **desktop browser with mouse and keyboard**; tablets and phones must remain
usable. The dash rules (`docs/ui-guidelines.md`) are made for a small touch display
used while riding and only partly apply.

Rules are numbered (e.g. `E-L1`) so that reviews and checks can refer to them.

## Shared rules

These rules are defined once in `docs/ui-guidelines.md` (marked "shared" there)
and apply to the editor unchanged:

| Rule | Content |
|---|---|
| **C1** + colour token table | only the nine colour tokens, same meaning |
| **T1** (typeface only) | Montserrat Medium; the size steps of T1 do **not** apply, see E-T2 |
| **T4** | language German, umlauts and ß |
| **N1** | German number format, percentages with a space |

Everything below is specific to the editor.

## Layout (E-L)

- **E-L1** Target: desktop, viewport ≥ 1280×800. Three columns: preset list
  (240 px), work area (flexible), selection/tools panel (320 px).
- **E-L2** Tablet (768–1279 px): the preset list becomes a drawer; the panel moves
  below the work area.
- **E-L3** Phone (< 768 px): one column, all functions reachable; editing long
  patterns is allowed to be less comfortable.
- **E-L4** The page never scrolls horizontally. The ground view may scroll
  horizontally inside its own frame; the segment table scrolls vertically inside
  its frame with a sticky header.
- **E-L5** Spacing in steps of 4 px (4, 8, 12, 16, 24); radius 8 px for inputs and
  buttons, 12 px for panels.

## Typography (E-T)

- **E-T1** Typeface per T1. The font file is served by the dash from its own
  flash – no external fonts, the access point has no internet. Fallback: system
  sans-serif.
- **E-T2** Sizes: 13 px (hints, table header), 15 px (body, inputs, table),
  18 px (section titles), 22 px (preset name in the header).
- **E-T3** Numbers in tables and inputs use tabular figures
  (`font-variant-numeric: tabular-nums`) and are right-aligned, so columns line
  up and values do not jump.

## Numbers and input (E-N)

- **E-N1** Display per N1. Inputs accept both `,` and `.` as decimal separator.
- **E-N2** Units are shown next to the input (`cm`, `%`, `mm`), not inside the
  value.
- **E-N3** Lengths are entered in cm (ADR 0011); the total length is shown in m
  with two decimals.
- **E-N4** Invalid input is marked at the field with a short message; it is never
  silently corrected. Limits are shown as hint (e.g. `1 … 2048`).
- **E-N5** Numeric fields support arrow keys (±1, with Shift ±10) and the mouse
  wheel while focused.

## Colours (E-C)

Colours per C1 and the token table (shared rules). In addition:

- **E-C2** Pattern colours (segment colours) are content, not UI; they are shown
  with a thin `border` outline so that dark colours stay visible on the dark
  background.
- **E-C3** Selection: `accent` outline (2 px) in the table and in the ground view.

## Interaction (E-I)

- **E-I1** Mouse targets at least 28 px high; on touch devices (`pointer: coarse`)
  at least 44 px.
- **E-I2** Keyboard: every function reachable by keyboard, visible focus ring
  (`accent`). Shortcuts:

  | Shortcut | Function |
  |---|---|
  | Cmd/Ctrl+S | save |
  | Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z | undo, redo |
  | Cmd/Ctrl+A | select all segments |
  | Esc | clear selection / close dialog |
  | Delete/Backspace | delete selected segments (min. 1 remains) |

- **E-I3** Selection: click, Shift+click (range), Cmd/Ctrl+click (toggle), in
  the table and in the ground view.
- **E-I4** Hover shows a tooltip with the segment values in the ground view.

## States and feedback (E-S)

- **E-S1** Connection state to the dash is always visible in the header
  ("verbunden" / "lokal (ohne Roller)" / "Verbindung verloren").
- **E-S2** Unsaved changes: dot at the preset name, save button enabled; leaving
  asks for confirmation.
- **E-S3** Changes made on the dash appear within about 1 s; conflicts follow
  ADR 0017 (notice, then "überschreiben" / "neu laden").
- **E-S4** Every action gives feedback within 100 ms; saving shows success or the
  error text from the dash.

## Dialogs (E-D)

- **E-D1** Confirmation for deleting presets, overwriting after a conflict and
  discarding unsaved changes. Primary action right (desktop convention),
  destructive actions in `danger`.
- **E-D2** Esc closes, Enter confirms the primary action.
