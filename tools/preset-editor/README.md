# Preset editor

Web editor for patterns and overlays (ADR 0013, 0017; UI rules in
`docs/editor-guidelines.md`). Plain HTML/CSS/JavaScript with ES modules, no
dependencies, no build step. Later the dash serves these files over Wi-Fi.

## Run

From the repository root:

```sh
node tools/preset-editor/dev-server.mjs           # editor + simulated dash
node tools/preset-editor/dev-server.mjs --local   # editor only (local mode)
```

- Editor: <http://localhost:8080/>
- Simulated dash: <http://localhost:8080/mock> – select presets, move the overlay
  sliders as on the dash (built-in overlays get copied to "Name (2)"), or change
  a preset "elsewhere" to try the conflict handling.
- Options: `--port <n>`, `--latency <ms>` (simulated UART delay for writes, default 150).

The simulated dash keeps everything in memory; restarting the server resets it.
In local mode the user presets are kept in the browser (localStorage).

## Tests

```sh
node --test tools/preset-editor/test/
```

## Files

| File | Content |
|---|---|
| `model.js` | pure functions: validation, segment operations, tools, JSON, undo |
| `render.js` | strip rendering (box filter, overlay) – same algorithm as core |
| `mock-dash.mjs` | behaviour of dash + core: slots, revisions, 409 conflicts, auto-copy |
| `api.js` | dash HTTP API and local mode |
| `index.html`, `app.js`, `style.css` | user interface |
| `dev-server.mjs` | development server (not part of the dash firmware) |
