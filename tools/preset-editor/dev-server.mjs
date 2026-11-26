#!/usr/bin/env node
/*
 * Development server for the preset editor (ADR 0017). Node only, no dependencies.
 *
 *   node tools/preset-editor/dev-server.mjs            editor + mock dash API
 *   node tools/preset-editor/dev-server.mjs --local    editor only (local mode, files)
 *   options: --port <n> (default 8080), --latency <ms> (default 150, simulated UART)
 *
 * Pages:
 *   http://localhost:8080/        the editor
 *   http://localhost:8080/mock    control page: act "on the dash" (overlay sliders,
 *                                 select presets, change a preset from elsewhere)
 *
 * Extra paths (dev server only, not on the dash):
 *   /presets/index.json   built-in presets from presets/ in the repository
 *   /fonts/…              font files from dash/ui/assets
 */
import { readFileSync, readdirSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { MockDash } from './mock-dash.mjs';
import { parsePreset } from './model.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '../..');

const args = process.argv.slice(2);
const opt = (name, def) => {
    const i = args.indexOf(name);
    return i >= 0 && args[i + 1] !== undefined ? args[i + 1] : def;
};
const LOCAL = args.includes('--local');
const PORT = Number(opt('--port', 8080));
const LATENCY = Number(opt('--latency', 150));

const TYPES = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.ttf': 'font/ttf',
    '.svg': 'image/svg+xml',
};

/* Built-in presets: presets/{patterns,overlays}/*.json, slot order = file name order */
function loadBuiltins() {
    const out = { patterns: [], overlays: [] };
    for (const cat of Object.keys(out)) {
        const dir = path.join(repo, 'presets', cat);
        let files = [];
        try {
            files = readdirSync(dir).filter((f) => f.endsWith('.json')).sort();
        } catch {
            /* no presets directory: no built-ins */
        }
        for (const f of files) {
            try {
                out[cat].push(parsePreset(readFileSync(path.join(dir, f), 'utf8')));
            } catch (e) {
                console.warn(`presets/${cat}/${f}: ${e.message} – skipped`);
            }
        }
    }
    return out;
}

const builtins = loadBuiltins();
const dash = new MockDash(builtins);

function send(res, status, body, type = 'application/json; charset=utf-8') {
    const data = typeof body === 'string' || Buffer.isBuffer(body) ? body : JSON.stringify(body);
    res.writeHead(status, { 'Content-Type': type, 'Cache-Control': 'no-store' });
    res.end(data);
}

async function readJson(req) {
    const chunks = [];
    for await (const c of req) chunks.push(c);
    const text = Buffer.concat(chunks).toString('utf8');
    if (text === '') return null;
    return JSON.parse(text);
}

const delay = (ms) => new Promise((r) => setTimeout(r, ms));

/* ---- API of ADR 0013 ---------------------------------------------------------- */

async function api(req, res, parts) {
    const [first, second] = parts;
    const method = req.method;
    let body = null;
    if (method === 'PUT' || method === 'POST') {
        try {
            body = await readJson(req);
        } catch {
            return send(res, 400, { error: 'Kein gültiges JSON' });
        }
    }
    let r;
    if (method === 'GET' && first === 'state' && !second) r = dash.state();
    else if (method === 'GET' && first === 'settings' && !second) r = { status: 200, body: dash.settings };
    else if (method === 'GET' && first === 'export' && !second) r = dash.exportAll();
    else if (method === 'POST' && first === 'time' && !second) r = dash.setTime(body);
    else if (method === 'POST' && first === 'preview' && second) r = dash.setPreview(second, body);
    else if (dash.validCategory(first) && !second && method === 'GET') r = { status: 200, body: dash.list(first) };
    else if (dash.validCategory(first) && second !== undefined && /^\d+$/.test(second)) {
        const slot = Number(second);
        if (method === 'GET') r = dash.get(first, slot);
        else if (method === 'PUT') r = dash.put(first, slot, body);
        else if (method === 'DELETE') r = dash.delete(first, slot);
    }
    if (!r) return send(res, 404, { error: 'Unbekannter API-Pfad' });
    /* writes go over UART to core and wait for its ACK */
    if (method !== 'GET' && LATENCY > 0) await delay(LATENCY);
    if (method !== 'GET') console.log(`${method} /api/${parts.join('/')} → ${r.status}${r.body?.error ? ' ' + r.body.error : ''}`);
    return send(res, r.status, r.body);
}

/* ---- control page "on the dash" ------------------------------------------------- */

async function mockAction(req, res, action) {
    let body;
    try {
        body = (await readJson(req)) ?? {};
    } catch {
        return send(res, 400, { error: 'Kein gültiges JSON' });
    }
    let r;
    if (action === 'overlay') r = dash.dashSetOverlay(body.values ?? {});
    else if (action === 'select') r = dash.select(body.category, Number(body.slot));
    else if (action === 'touch') r = dash.touch(body.category, Number(body.slot));
    else if (action === 'layout') r = dash.setLayout({ main_leds: Number(body.main_leds) });
    else return send(res, 404, { error: 'Unbekannte Aktion' });
    console.log(`[dash] ${action} ${JSON.stringify(body)} → ${r.status}${r.body?.error ? ' ' + r.body.error : ''}`);
    return send(res, r.status, r.body);
}

const MOCK_PAGE = `<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>dash simulieren</title>
<style>
  :root { --bg:#0d1117; --card:#161b22; --border:#30363d; --text:#e6edf3; --muted:#8b949e; --accent:#2dd4bf; --danger:#ef4444; }
  body { background:var(--bg); color:var(--text); font:15px/1.4 system-ui, sans-serif; margin:0; padding:16px; }
  main { max-width:560px; margin:0 auto; display:grid; gap:16px; }
  section { background:var(--card); border:1px solid var(--border); border-radius:12px; padding:16px; display:grid; gap:12px; }
  h1 { font-size:22px; margin:0; } h2 { font-size:18px; margin:0; }
  p { color:var(--muted); margin:0; font-size:13px; }
  label { display:grid; grid-template-columns:120px 1fr 56px; gap:8px; align-items:center; }
  output { text-align:right; font-variant-numeric:tabular-nums; }
  select, button { font:inherit; color:var(--text); background:var(--bg); border:1px solid var(--border); border-radius:8px; min-height:32px; padding:0 12px; }
  button:hover { border-color:var(--accent); }
  input[type=range] { accent-color:var(--accent); width:100%; }
  .row { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
  #msg { min-height:20px; color:var(--accent); } #msg.err { color:var(--danger); }
</style></head>
<body><main>
<h1>dash simulieren</h1>
<p>Löst Aktionen aus, als kämen sie vom Touch-Display des dash. Der Editor im anderen Fenster sieht sie innerhalb von etwa 1 s.</p>
<div id="msg"></div>
<section>
  <h2>Muster wählen</h2>
  <div class="row"><select id="pat"></select><button data-touch="patterns">Am dash ändern (Konflikt testen)</button></div>
</section>
<section>
  <h2>LED-Streifen</h2>
  <p>Länge des Hauptteils, wie in den dash-Einstellungen (ADR 0018). Der Editor passt Vorschau und Overlay-Kurve an.</p>
  <label><span>Hauptteil</span><input type="range" id="main" min="36" max="144" step="1"><output id="main-out"></output></label>
</section>
<section>
  <h2>Overlay</h2>
  <div class="row"><select id="ovl"></select><button data-touch="overlays">Am dash ändern (Konflikt testen)</button></div>
  <p>Regler schreiben ins aktive Overlay. Bei einem eingebauten Overlay entsteht zuerst eine Kopie „Name (2)“.</p>
  <div id="sliders"></div>
</section>
</main>
<script>
const KEYS = [['level_start','Pegel Anfang','%',100],['level_mid','Pegel Mitte','%',100],['level_end','Pegel Ende','%',100],
              ['fade_start_mm','Übergang Anfang','mm',500],['fade_end_mm','Übergang Ende','mm',500]];
const msg = (t, err) => { const m = document.getElementById('msg'); m.textContent = t; m.className = err ? 'err' : ''; };
async function call(action, body) {
  const r = await fetch('/mock/' + action, { method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body) });
  const j = await r.json();
  if (!r.ok) msg(j.error, true); else msg('OK');
  await refresh();
}
const sliders = document.getElementById('sliders');
for (const [k, label, unit, max] of KEYS) {
  const l = document.createElement('label');
  l.innerHTML = '<span>' + label + '</span><input type="range" min="0" max="' + max + '" step="' + (unit === '%' ? 1 : 10) + '" data-k="' + k + '"><output></output>';
  sliders.append(l);
}
sliders.addEventListener('change', (e) => { const k = e.target.dataset.k; if (k) call('overlay', { values: { [k]: Number(e.target.value) } }); });
sliders.addEventListener('input', (e) => { e.target.nextElementSibling.textContent = e.target.value + ' ' + (e.target.dataset.k.endsWith('mm') ? 'mm' : '%'); });
function fill(sel, list, active) {
  sel.innerHTML = '';
  for (const e of list.filter((x) => x.kind !== 'empty')) {
    const o = new Option((e.kind === 'builtin' ? '🔒 ' : '') + e.name + '  (Slot ' + e.slot + ', Rev. ' + e.revision + ')', e.slot);
    o.selected = e.slot === active; sel.append(o);
  }
}
async function refresh() {
  const s = await (await fetch('/api/state')).json();
  fill(document.getElementById('pat'), s.patterns, s.settings.active_pattern);
  fill(document.getElementById('ovl'), s.overlays, s.settings.active_overlay);
  if (document.activeElement !== main) { main.value = s.settings.main_leds; document.getElementById('main-out').textContent = main.value + ' LEDs'; }
  for (const input of sliders.querySelectorAll('input')) {
    if (document.activeElement === input) continue;
    input.value = s.settings.overlay?.[input.dataset.k] ?? 0;
    input.nextElementSibling.textContent = input.value + ' ' + (input.dataset.k.endsWith('mm') ? 'mm' : '%');
  }
}
const main = document.getElementById('main');
main.addEventListener('input', () => { document.getElementById('main-out').textContent = main.value + ' LEDs'; });
main.addEventListener('change', () => call('layout', { main_leds: main.value }));
document.getElementById('pat').onchange = (e) => call('select', { category:'patterns', slot:e.target.value });
document.getElementById('ovl').onchange = (e) => call('select', { category:'overlays', slot:e.target.value });
for (const b of document.querySelectorAll('[data-touch]')) {
  b.onclick = () => { const cat = b.dataset.touch; call('touch', { category:cat, slot:document.getElementById(cat === 'patterns' ? 'pat' : 'ovl').value }); };
}
refresh(); setInterval(refresh, 1000);
</script></body></html>`;

/* ---- static files ------------------------------------------------------------------ */

async function serveFile(res, root, rel) {
    const file = path.resolve(root, '.' + path.posix.normalize('/' + rel));
    if (!file.startsWith(root + path.sep) && file !== root) return send(res, 403, 'Verboten', 'text/plain');
    try {
        const data = await readFile(file);
        return send(res, 200, data, TYPES[path.extname(file)] ?? 'application/octet-stream');
    } catch {
        return send(res, 404, 'Nicht gefunden', 'text/plain; charset=utf-8');
    }
}

const server = http.createServer(async (req, res) => {
    try {
        const url = new URL(req.url, 'http://localhost');
        const p = decodeURIComponent(url.pathname);
        if (p.startsWith('/api/')) {
            if (LOCAL) return send(res, 404, { error: 'Lokaler Modus, keine dash-API' });
            return await api(req, res, p.slice(5).split('/').filter(Boolean));
        }
        if (p === '/mock' && !LOCAL) return send(res, 200, MOCK_PAGE, TYPES['.html']);
        if (p.startsWith('/mock/') && req.method === 'POST' && !LOCAL) return await mockAction(req, res, p.slice(6));
        if (p === '/presets/index.json') return send(res, 200, builtins);
        if (p.startsWith('/fonts/')) return await serveFile(res, path.join(repo, 'dash/ui/assets'), p.slice(7));
        if (p.startsWith('/test/') || p === '/dev-server.mjs') return send(res, 404, 'Nicht gefunden', 'text/plain; charset=utf-8');
        return await serveFile(res, here, p === '/' ? 'index.html' : p);
    } catch (e) {
        console.error(e);
        return send(res, 500, { error: 'Interner Fehler' });
    }
});

server.listen(PORT, '127.0.0.1', () => {
    console.log(`Preset-Editor: http://localhost:${PORT}/  (${LOCAL ? 'lokaler Modus' : 'mit simuliertem dash'})`);
    if (!LOCAL) console.log(`dash simulieren: http://localhost:${PORT}/mock`);
    console.log(`eingebaute Presets: ${builtins.patterns.length} Muster, ${builtins.overlays.length} Overlays`);
});
