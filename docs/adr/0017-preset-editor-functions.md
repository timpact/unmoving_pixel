# ADR 0017: Preset editor – functions and interaction

- Status: Accepted
- Date: 2026-11-26

## Context

ADR 0013 decided that the preset editor is a web app served by the dash over
Wi-Fi, defined the HTTP API and the JSON formats. It did not define how the editor
is operated. Requirements from the rider:

- A control for the number of segments
- A button to save, a field for the pattern name
- Editing several segments at once (e.g. colour and brightness)
- Applying a basic structure to a set of segments (e.g. every second segment has
  brightness 0)
- The editor is mainly used in a desktop browser; the dash GUI rules (4.3" touch
  display, used while riding) do not fit it

Overlays (ADR 0012) can also be changed on the dash. Both places must work on the
same presets without overwriting each other unnoticed.

## Decision

### Scope of the first version

Patterns and overlays, both in the same editor (two tabs: "Muster", "Overlays").

### Layout (desktop first)

```
┌──────────────┬──────────────────────────────────────────────┬───────────────┐
│ Preset list  │ Name [__________]  Abschnitte [-][ 24 ][+]   │ Selection     │
│ (built-in 🔒, │ Gesamtlänge 3,60 m   [Am Strip testen] [Speichern] │ properties    │
│  user, free) │──────────────────────────────────────────────│ (bulk edit)   │
│              │ Ground view: whole pattern, proportional      │               │
│ New          │ Strip preview: 72 LEDs, animated ride         │ Tools         │
│ Duplicate    │──────────────────────────────────────────────│ (every n-th,  │
│ Delete       │ Segment table: # | length cm | colour | %     │  gradient,    │
│ Import/Export│                                              │  repeat, …)   │
└──────────────┴──────────────────────────────────────────────┴───────────────┘
```

Details and sizes: `docs/editor-guidelines.md`.

### Patterns

- **Name:** text field, required, max. 16 bytes UTF-8 (ADR 0011); a counter
  shows the remaining bytes.
- **Number of segments:** numeric field with − / + buttons; mouse wheel and arrow
  keys change it by 1 (with Shift by 10). Range 1 … 2048. Increasing appends copies
  of the last segment; decreasing removes segments from the end (with undo).
  The total length of the pattern is shown next to it.
- **Segment table:** one row per segment: index, length (cm), colour (colour
  picker + hex), brightness (%). Values can be edited directly in the table.
- **Ground view:** the whole pattern drawn proportionally as a band (zoomable,
  horizontally scrollable for long patterns); segments can be selected there too.
- **Strip preview:** 72 LEDs at 6.94 mm pitch with the selected overlay, animated
  as a ride at adjustable speed. It uses the same rendering as core (box filter,
  ADR 0011), so the preview matches the strip.

### Selection and bulk editing

- Click selects one segment, Shift+click a range, Cmd/Ctrl+click toggles single
  segments, Cmd/Ctrl+A selects all. Works in the table and in the ground view.
- The properties panel shows length, colour and brightness of the selection;
  differing values show "gemischt". A value entered there is set for all selected
  segments.

### Tools

Tools work on the selection (or on all segments if nothing is selected):

| Tool | Parameters | Example |
|---|---|---|
| **Jeder n-te** | n (≥ 2), start position k (1 … n), property (length, colour or brightness) and value | every 2nd from the 2nd: brightness 0 |
| **Verlauf** | colour A → B and/or brightness A → B over the selection, linear | red → blue over 12 segments |
| **Gruppe wiederholen** | number of repetitions; the selected contiguous range is inserted n times after itself | 3 segments × 4 |
| **Länge verteilen** | either the same length for every segment, or a total length split evenly (remainder spread over the first segments) | 10 m over 50 segments |

Colour gradients are interpolated in linear light (as the strip renders them).
All limits of ADR 0011 are checked; a tool that would exceed 2048 segments is
rejected with a message.

### Undo, saving, unsaved changes

- Undo/redo (Cmd/Ctrl+Z, Shift+Cmd/Ctrl+Z), at least 100 steps; every tool
  application and bulk edit is one step.
- **Speichern** (Cmd/Ctrl+S) writes the preset to its user slot. A built-in preset
  cannot be overwritten: saving it creates a new user preset (copy).
- Unsaved changes are marked (dot at the name, enabled save button); leaving the
  preset or closing the page asks for confirmation.
- **Am Strip testen** sends the current state to the preview slot of core
  (pattern) or as active overlay without saving (overlay), ADR 0011/0013.

### Overlays

- Name, five parameters (levels at start, middle and end; fade lengths at start
  and end) as slider plus number field, curve plot over the 72 LEDs, and the
  strip preview with a selectable pattern.
- Same save, undo and preset-list behaviour as for patterns.

### Dash and editor working on the same presets

- **Dash writes into the preset** (changes ADR 0012): the overlay sliders on the
  dash change the active overlay preset itself, saved 2 s after the last change.
  There is no separate "modified" state any more.
- **Built-in overlays:** the first change on the dash creates a user copy named
  "<name> (2)" (or the next free number, shortened to 16 bytes), selects it and
  applies the change there. The built-in preset stays unchanged. If no user slot is
  free, the dash shows "Kein freier Platz – im Editor ein Overlay löschen" and
  keeps the built-in values.
- **Revision per preset:** core keeps a revision counter for every preset slot,
  incremented on every write (ADR 0004).
- **Editor stays up to date:** the editor polls `GET /api/state` every second
  (settings, preset list with revisions). If the open preset changed elsewhere:
  - without unsaved changes in the editor: it reloads the preset silently;
  - with unsaved changes: a notice "Am dash geändert" is shown; saving then asks
    "überschreiben" or "neu laden".
- **Saving is conditional:** `PUT` carries the revision the edit was based on; if
  it no longer matches, the dash answers `409 Conflict` and the editor asks as
  above.

### Development and tests

- Plain HTML/CSS/JavaScript with ES modules, no framework and no build step
  (ADR 0013). Files in `tools/preset-editor/`:
  - `model.js`: pure functions – segment operations, tools, validation, JSON
    conversion, undo stack
  - `render.js`: strip rendering (box filter, overlay; same algorithm as core)
  - `api.js`: dash API and local mode
  - `index.html`, `app.js`, `style.css`: user interface
- **Local development without the scooter:** `node tools/preset-editor/dev-server.mjs`
  (Node, no dependencies) serves the editor and a mock dash API in memory that
  behaves like dash and core (slots, built-in read-only, revisions, conflicts,
  auto-copy). It can simulate changes "from the dash" to test conflicts.
- **Local mode without any server:** opening `index.html` directly works on JSON
  files only (import, export, presets from `presets/`).
- **Tests:** `node --test tools/preset-editor/test/` for `model.js` and
  `render.js` (tools, limits, JSON round trip, gradient, rendering against
  reference values shared with core).

## Consequences

- One editor for patterns and overlays, comfortable on a desktop, usable on a
  tablet.
- The dash and the editor cannot silently overwrite each other's changes.
- On the dash, adjusting an overlay is direct: no "save" step and no "modified"
  state; built-in overlays are protected by automatic copies.
- Automatic copies can fill the 8 user overlay slots; deleting happens in the
  editor.
- Protocol and API need revisions per preset and a conflict response
  (ADR 0004, 0013).
- The rendering exists twice (C on core, JavaScript in the editor); shared test
  vectors keep them consistent.
