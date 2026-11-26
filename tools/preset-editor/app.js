/*
 * Preset editor – user interface (ADR 0013, 0017, docs/editor-guidelines.md).
 *
 * State per tab ("patterns", "overlays"): one open document with its own undo
 * stack. The dash is polled every second (E-S3); changes made elsewhere are
 * loaded silently, or shown as notice if there are unsaved changes.
 */
import { ApiError, connect } from './api.js';
import {
    LIMITS, UndoStack, applyToSelection, clickSelect, copyName, deleteSegments, formatNumber,
    hexToRgb, linearToSrgb, newOverlay, newPattern, normalizeColor, parseNumber, parsePresetFile,
    presetFileName, selectionSummary, serializePreset, setSegmentCount, srgbToLinear, toolDistributeLength,
    toolEveryNth, toolGradient, toolRepeatGroup, totalLengthCm, utf8Length, validateOverlay, validatePattern,
} from './model.js';
import { STRIP, overlayFactor, renderStrip } from './render.js';

const $ = (id) => document.getElementById(id);
const CATS = ['patterns', 'overlays'];
const POLL_MS = 1000;
const MERGE_MS = 1000;   /* repeated edits of one field within 1 s are one undo step */
const ROW_H = 36;

/* ---- state ------------------------------------------------------------------------- */

let api = null;
let conn = 'connecting';   /* 'ok' | 'local' | 'lost' */
const lists = { patterns: [], overlays: [] };
let settings = null;
let tab = 'patterns';
const docs = {};
const presetCache = new Map();   /* "cat:slot:rev" → preset, for the preview */

function makeDoc(cat, preset, { slot = null, kind = null, revision = null } = {}) {
    return {
        cat,
        slot,
        kind,                       /* 'builtin' | 'user' | null (not stored yet) */
        baseRevision: revision,
        preset: structuredClone(preset),
        savedJson: kind === null ? null : JSON.stringify(preset),
        remote: null,               /* { revision } when changed elsewhere with local edits */
        selection: [],
        anchor: null,
        undo: new UndoStack(),
        merge: { key: null, time: 0 },
        saving: false,
    };
}

const doc = () => docs[tab];
const isDirty = (d) => d.savedJson === null || JSON.stringify(d.preset) !== d.savedJson;

/* ---- helpers ------------------------------------------------------------------------- */

function h(tag, attrs = {}, ...children) {
    const el = document.createElement(tag);
    for (const [k, v] of Object.entries(attrs)) {
        if (k === 'class') el.className = v;
        else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
        else if (v === true) el.setAttribute(k, '');
        else if (v !== false && v !== null && v !== undefined) el.setAttribute(k, v);
    }
    el.append(...children.filter((c) => c !== null && c !== undefined));
    return el;
}

let toastTimer = null;
function toast(text, error = false) {
    const t = $('toast');
    t.textContent = text;
    t.className = 'toast' + (error ? ' error' : '');
    t.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => { t.hidden = true; }, error ? 6000 : 3000);
}

/* Confirmation dialog (E-D1, E-D2): buttons left to right, the last one is primary */
function ask(title, text, buttons) {
    const dlg = $('dialog');
    $('dialog-title').textContent = title;
    $('dialog-text').textContent = text;
    const box = $('dialog-buttons');
    box.replaceChildren(...buttons.map((b, i) => h('button', {
        class: 'btn' + (b.kind === 'danger' ? ' danger' : b.kind === 'primary' ? ' primary' : ''),
        value: b.value,
        autofocus: i === buttons.length - 1,
    }, b.label)));
    return new Promise((resolve) => {
        dlg.onclose = () => resolve(dlg.returnValue || null);
        dlg.returnValue = '';
        dlg.showModal();
    });
}

function download(name, text) {
    const url = URL.createObjectURL(new Blob([text], { type: 'application/json' }));
    const a = h('a', { href: url, download: name });
    document.body.append(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/* Colour of a segment as it looks on the strip (colour × perceptual brightness) */
function displayColor(seg) {
    const b = srgbToLinear(seg.brightness / 100);
    const rgb = hexToRgb(seg.color).map((c) => Math.round(linearToSrgb(srgbToLinear(c / 255) * b) * 255));
    return `rgb(${rgb.join(',')})`;
}

function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
}

/* Canvas with device-pixel resolution; returns the 2D context in CSS pixels */
function fitCanvas(canvas, width, height, setWidth = false) {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(width));
    if (canvas.width !== Math.round(w * dpr) || canvas.height !== Math.round(height * dpr)) {
        canvas.width = Math.round(w * dpr);
        canvas.height = Math.round(height * dpr);
    }
    if (setWidth) canvas.style.width = w + 'px';
    const ctx = canvas.getContext('2d');
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    return ctx;
}

/*
 * Integer field (E-N1, E-N4, E-N5): accepts "," and ".", arrow keys ±1 (Shift ±10),
 * mouse wheel while focused. Invalid input is marked and not applied.
 */
function intField(input, { min, max, get, set, errorEl = null, label = '' }) {
    const show = (msg) => {
        input.classList.toggle('invalid', !!msg);
        input.title = msg || '';
        if (errorEl) errorEl.textContent = msg || '';
    };
    const commit = (value) => {
        if (!Number.isInteger(value) || value < min || value > max) {
            show(`${label}${label ? ': ' : ''}${formatNumber(min)} … ${formatNumber(max)}`);
            return false;
        }
        show('');
        set(value);
        return true;
    };
    input.addEventListener('change', () => {
        if (input.value.trim() === '') { show(''); return; }   /* empty = "gemischt" kept */
        commit(parseNumber(input.value));
    });
    input.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') { input.dispatchEvent(new Event('change')); return; }
        if (e.key !== 'ArrowUp' && e.key !== 'ArrowDown') return;
        e.preventDefault();
        const cur = parseNumber(input.value);
        const base = Number.isNaN(cur) ? (get() ?? min) : cur;
        const step = (e.shiftKey ? 10 : 1) * (e.key === 'ArrowUp' ? 1 : -1);
        const v = Math.min(max, Math.max(min, Math.round(base) + step));
        input.value = String(v);
        commit(v);
    });
    input.addEventListener('wheel', (e) => {
        if (document.activeElement !== input) return;
        e.preventDefault();
        const cur = parseNumber(input.value);
        const base = Number.isNaN(cur) ? (get() ?? min) : cur;
        const v = Math.min(max, Math.max(min, Math.round(base) + (e.deltaY < 0 ? 1 : -1) * (e.shiftKey ? 10 : 1)));
        input.value = String(v);
        commit(v);
    }, { passive: false });
    input.addEventListener('blur', () => { if (!input.classList.contains('invalid')) show(''); });
    return { show };
}

/* ---- changes and undo (ADR 0017) ------------------------------------------------------- */

/* Applies a change to the open document as one undo step (merged for the same key) */
function change(fn, mergeKey = null) {
    const d = doc();
    const now = Date.now();
    const merge = mergeKey !== null && d.merge.key === mergeKey && now - d.merge.time < MERGE_MS;
    const before = { preset: d.preset, selection: d.selection };
    const result = fn(structuredClone(d.preset));
    if (result === undefined) return;
    if (!merge) d.undo.push(before);
    d.merge = { key: mergeKey, time: now };
    if (result.preset) {
        d.preset = result.preset;
        if (result.selection) d.selection = result.selection;
    } else {
        d.preset = result;
    }
    d.selection = d.selection.filter((i) => d.preset.type !== 'pattern' || i < d.preset.segments.length);
    render();
}

function undo(redo = false) {
    const d = doc();
    const cur = { preset: d.preset, selection: d.selection };
    const next = redo ? d.undo.redo(cur) : d.undo.undo(cur);
    d.preset = next.preset;
    d.selection = next.selection;
    d.merge = { key: null, time: 0 };
    render();
}

/* ---- connection and polling (E-S1, E-S3) ----------------------------------------------- */

function setConn(state) {
    conn = state;
    const el = $('conn');
    el.className = 'conn ' + (state === 'ok' ? 'ok' : state === 'lost' ? 'lost' : '');
    el.querySelector('.text').textContent = {
        ok: 'verbunden', local: 'lokal (ohne Roller)', lost: 'Verbindung verloren', connecting: 'verbinde …',
    }[state];
    renderHead();
}

async function refreshState() {
    let s;
    try {
        s = await api.state();
    } catch (e) {
        if (api.mode === 'dash' && conn !== 'lost') setConn('lost');
        return;
    }
    if (api.mode === 'dash' && conn !== 'ok') setConn('ok');
    settings = s.settings;
    const changedList = CATS.some((c) => JSON.stringify(lists[c]) !== JSON.stringify(s[c]));
    lists.patterns = s.patterns;
    lists.overlays = s.overlays;
    for (const cat of CATS) await syncDoc(cat);
    if (changedList && docs[tab]) {
        renderList();
        renderPreviewSelect();
    }
}

/* Open document changed or deleted elsewhere? (ADR 0017) */
async function syncDoc(cat) {
    const d = docs[cat];
    if (!d || d.slot === null || d.saving) return;
    const entry = lists[cat].find((e) => e.slot === d.slot);
    if (!entry || entry.kind === 'empty') {
        /* deleted elsewhere: keep the content as an unsaved preset */
        d.slot = null;
        d.kind = null;
        d.baseRevision = null;
        d.savedJson = null;
        d.remote = null;
        if (cat === tab) {
            toast('Das Preset wurde anderswo gelöscht – Inhalt bleibt als neues Preset offen');
            render();
        }
        return;
    }
    if (entry.revision === d.baseRevision) return;
    if (!isDirty(d)) {
        try {
            const r = await api.get(cat, d.slot);
            if (docs[cat] !== d || isDirty(d)) return;
            d.preset = r.preset;
            d.baseRevision = r.revision;
            d.savedJson = JSON.stringify(r.preset);
            d.remote = null;
            d.selection = d.selection.filter((i) => cat !== 'patterns' || i < r.preset.segments.length);
            if (cat === tab) render();
        } catch {
            /* next poll tries again */
        }
    } else if (d.remote?.revision !== entry.revision) {
        d.remote = { revision: entry.revision };
        if (cat === tab) renderHead();
    }
}

/* ---- presets: open, new, save, delete ----------------------------------------------------- */

async function confirmDiscard(d = doc()) {
    if (!isDirty(d)) return true;
    const r = await ask('Änderungen verwerfen?', `„${d.preset.name}“ hat ungespeicherte Änderungen.`, [
        { label: 'Abbrechen', value: 'cancel' },
        { label: 'Verwerfen', value: 'discard', kind: 'danger' },
    ]);
    return r === 'discard';
}

async function openPreset(cat, slot) {
    const d = docs[cat];
    if (d.slot === slot && d.kind !== null) return;
    if (!(await confirmDiscard(d))) return;
    try {
        const r = await api.get(cat, slot);
        docs[cat] = makeDoc(cat, r.preset, { slot, kind: r.builtin ? 'builtin' : 'user', revision: r.revision });
        document.body.classList.remove('drawer-open');
        render();
    } catch (e) {
        toast(e.message, true);
    }
}

function uniqueName(cat, name) {
    const names = lists[cat].filter((e) => e.kind !== 'empty').map((e) => e.name);
    return names.includes(name) ? copyName(name, names) : name;
}

async function newDoc() {
    if (!(await confirmDiscard())) return;
    const p = tab === 'patterns' ? newPattern() : newOverlay();
    p.name = uniqueName(tab, p.name);
    docs[tab] = makeDoc(tab, p);
    render();
    $('name').select();
}

function duplicateDoc() {
    const d = doc();
    const p = structuredClone(d.preset);
    const names = lists[tab].filter((e) => e.kind !== 'empty').map((e) => e.name);
    p.name = copyName(p.name, [...names, d.preset.name]);
    docs[tab] = makeDoc(tab, p);
    render();
    toast(`Kopie „${p.name}“ angelegt – noch nicht gespeichert`);
}

function errorsOf(d) {
    return d.cat === 'patterns' ? validatePattern(d.preset) : validateOverlay(d.preset);
}

async function save() {
    const d = doc();
    if (d.saving) return;
    const errors = errorsOf(d);
    if (errors.length > 0) {
        toast('Nicht gespeichert: ' + errors[0].message + (errors.length > 1 ? ` (+${errors.length - 1} weitere)` : ''), true);
        render();
        return;
    }
    let slot = d.slot;
    let base = d.baseRevision;
    let preset = d.preset;
    if (d.kind !== 'user') {
        /* new preset, or copy of a built-in one (ADR 0017) */
        const free = lists[d.cat].find((e) => e.kind === 'empty');
        if (!free) {
            toast(`Kein freier Platz – ein eigenes ${d.cat === 'patterns' ? 'Muster' : 'Overlay'} löschen`, true);
            return;
        }
        slot = free.slot;
        base = null;
        if (d.kind === 'builtin') preset = { ...preset, name: uniqueName(d.cat, preset.name) };
    }
    await putPreset(d, slot, base, preset, d.preset);
}

/* `before` is the document state the save started from; edits made meanwhile stay */
async function putPreset(d, slot, base, preset, before, retry = true) {
    d.saving = true;
    renderHead();
    let conflict = null;
    try {
        const r = await api.put(d.cat, slot, base, preset);
        d.slot = slot;
        d.kind = 'user';
        d.baseRevision = r.revision;
        if (d.preset === before) d.preset = preset;
        d.savedJson = JSON.stringify(preset);
        d.remote = null;
        toast(`„${preset.name}“ gespeichert`);
    } catch (e) {
        if (e instanceof ApiError && e.status === 409) conflict = e.body ?? {};
        else toast('Speichern fehlgeschlagen: ' + e.message, true);
    } finally {
        d.saving = false;
    }
    if (conflict && base === null && retry) {
        /* the free slot was taken meanwhile: take the next free one */
        await refreshState();
        const free = lists[d.cat].find((e) => e.kind === 'empty');
        if (free) return putPreset(d, free.slot, null, preset, before, false);
        toast('Kein freier Platz mehr', true);
    } else if (conflict) {
        renderHead();
        await resolveConflict(d, slot, preset, before, conflict);
        return undefined;
    }
    await refreshState();
    render();
    return undefined;
}

/* 409: changed elsewhere since the edit started (ADR 0017, E-D1) */
async function resolveConflict(d, slot, preset, before, body) {
    const choice = await ask('Am dash geändert',
        `„${preset.name}“ wurde inzwischen anderswo geändert.\nÜberschreiben ersetzt diese Änderung, Neu laden verwirft deine.`, [
            { label: 'Abbrechen', value: 'cancel' },
            { label: 'Neu laden', value: 'reload' },
            { label: 'Überschreiben', value: 'overwrite', kind: 'danger' },
        ]);
    if (choice === 'overwrite') {
        await putPreset(d, slot, body?.revision ?? null, preset, before);
    } else if (choice === 'reload') {
        await reloadDoc(d);
    }
}

/* Loads the stored version; the local state stays reachable with undo */
async function reloadDoc(d = doc()) {
    if (d.slot === null) return;
    try {
        const r = await api.get(d.cat, d.slot);
        d.undo.push({ preset: d.preset, selection: d.selection });
        d.preset = r.preset;
        d.baseRevision = r.revision;
        d.savedJson = JSON.stringify(r.preset);
        d.remote = null;
        d.selection = [];
        render();
        toast('Neu geladen – Rückgängig holt deinen Stand zurück');
    } catch (e) {
        toast(e.message, true);
    }
}

async function deletePreset() {
    const d = doc();
    if (d.kind !== 'user') return;
    const r = await ask('Preset löschen?', `„${d.preset.name}“ wird auf dem Roller gelöscht. Das lässt sich nicht rückgängig machen.`, [
        { label: 'Abbrechen', value: 'cancel' },
        { label: 'Löschen', value: 'delete', kind: 'danger' },
    ]);
    if (r !== 'delete') return;
    try {
        await api.delete(d.cat, d.slot);
        toast(`„${d.preset.name}“ gelöscht`);
        const p = d.cat === 'patterns' ? newPattern() : newOverlay();
        docs[d.cat] = makeDoc(d.cat, p);
        await refreshState();
        docs[d.cat].preset.name = uniqueName(d.cat, p.name);
        render();
    } catch (e) {
        toast(e.message, true);
    }
}

async function tryOnStrip() {
    const d = doc();
    const errors = errorsOf(d);
    if (errors.length > 0) { toast(errors[0].message, true); return; }
    try {
        await api.preview(d.cat, d.preset);
        toast(d.cat === 'patterns' ? 'Läuft am Strip (Vorschau, nicht gespeichert)' : 'Overlay am Strip aktiv (nicht gespeichert)');
    } catch (e) {
        toast(e.message, true);
    }
}

/* ---- import / export (ADR 0013) --------------------------------------------------------- */

function exportDoc() {
    const d = doc();
    download(presetFileName(d.preset.name), serializePreset(d.preset));
}

async function exportAll() {
    try {
        const bundle = await api.exportAll();
        if (bundle.presets.length === 0) { toast('Keine eigenen Presets vorhanden'); return; }
        download('unmoving-pixel-presets.json', serializePreset(bundle));
    } catch (e) {
        toast(e.message, true);
    }
}

async function importFiles(files) {
    const presets = [];
    for (const f of files) {
        try {
            presets.push(...parsePresetFile(await f.text()));
        } catch (e) {
            toast(`${f.name}: ${e.message}`, true);
            return;
        }
    }
    if (presets.length === 0) return;
    if (presets.length === 1) {
        const p = presets[0];
        const cat = p.type === 'pattern' ? 'patterns' : 'overlays';
        if (!(await confirmDiscard(docs[cat]))) return;
        docs[cat] = makeDoc(cat, p);
        setTab(cat);
        toast(`„${p.name}“ importiert – noch nicht gespeichert`);
        return;
    }
    const free = { patterns: lists.patterns.filter((e) => e.kind === 'empty'), overlays: lists.overlays.filter((e) => e.kind === 'empty') };
    const need = { patterns: presets.filter((p) => p.type === 'pattern').length, overlays: presets.filter((p) => p.type === 'overlay').length };
    if (need.patterns > free.patterns.length || need.overlays > free.overlays.length) {
        toast(`Zu wenig freie Plätze: ${need.patterns} Muster / ${need.overlays} Overlays, frei ${free.patterns.length} / ${free.overlays.length}`, true);
        return;
    }
    const r = await ask('Presets importieren?', `${need.patterns} Muster und ${need.overlays} Overlays in freie Plätze speichern?`, [
        { label: 'Abbrechen', value: 'cancel' },
        { label: 'Speichern', value: 'save', kind: 'primary' },
    ]);
    if (r !== 'save') return;
    let n = 0;
    try {
        for (const p of presets) {
            const cat = p.type === 'pattern' ? 'patterns' : 'overlays';
            const slot = free[cat].shift().slot;
            await api.put(cat, slot, null, { ...p, name: uniqueName(cat, p.name) });
            n += 1;
            await refreshState();
        }
        toast(`${n} Presets importiert`);
    } catch (e) {
        toast(`Nach ${n} Presets abgebrochen: ${e.message}`, true);
    }
    await refreshState();
}

/* ---- rendering --------------------------------------------------------------------------- */

function render() {
    document.body.dataset.tab = tab;
    for (const c of CATS) $('tab-' + c).setAttribute('aria-selected', String(c === tab));
    renderList();
    renderHead();
    if (tab === 'patterns') {
        renderGround();
        renderTable();
        renderSelection();
    } else {
        renderOverlayFields();
        renderCurve();
    }
    renderPreviewSelect();
}

function renderList() {
    const box = $('preset-list');
    const d = doc();
    const items = lists[tab];
    const activeSlot = settings ? (tab === 'patterns' ? settings.active_pattern : settings.active_overlay) : null;
    const item = (e) => h('button', {
        class: 'list-item', role: 'option', 'aria-selected': String(d.kind !== null && d.slot === e.slot),
        onclick: () => openPreset(tab, e.slot),
    },
    h('span', { class: 'name' }, (e.kind === 'builtin' ? '🔒 ' : '') + e.name),
    activeSlot === e.slot && conn === 'ok' ? h('span', { class: 'active', title: 'am Roller aktiv' }, '●') : null,
    tab === 'patterns' ? h('span', { class: 'meta' }, formatNumber(e.length_cm / 100, 2) + ' m') : null);
    const builtin = items.filter((e) => e.kind === 'builtin');
    const user = items.filter((e) => e.kind === 'user');
    const free = items.filter((e) => e.kind === 'empty').length;
    box.replaceChildren(
        h('div', { class: 'list-group' }, 'Eingebaut'),
        ...(builtin.length ? builtin.map(item) : [h('div', { class: 'list-free' }, 'keine')]),
        h('div', { class: 'list-group' }, 'Eigene'),
        ...user.map(item),
        h('div', { class: 'list-free' }, `${free} von ${free + user.length} Plätzen frei`),
    );
    $('act-del').disabled = d.kind !== 'user';
}

function renderHead() {
    const d = doc();
    if (!d) return;
    const name = $('name');
    if (document.activeElement !== name) name.value = d.preset.name;
    const bytes = utf8Length(d.preset.name);
    $('name-bytes').textContent = `${LIMITS.nameBytes - bytes} Byte frei`;
    $('name-bytes').classList.toggle('danger-text', bytes > LIMITS.nameBytes);
    const nameErr = errorsOf(d).find((e) => e.path === 'name');
    $('name-err').textContent = nameErr ? nameErr.message : '';
    name.classList.toggle('invalid', !!nameErr);
    const dirty = isDirty(d);
    $('dirty-dot').hidden = !dirty;   /* E-S2 */
    $('lock').hidden = d.kind !== 'builtin';
    $('save').disabled = !dirty || d.saving;
    $('save').textContent = d.saving ? 'Speichert …' : 'Speichern';
    $('try').disabled = api?.mode !== 'dash' || conn !== 'ok';
    $('try').title = api?.mode === 'dash' ? '' : 'Nur mit verbundenem Roller';
    $('undo').disabled = !d.undo.canUndo();
    $('redo').disabled = !d.undo.canRedo();
    $('notice').hidden = !d.remote;
    $('notice-text').textContent = 'Am dash geändert. Beim Speichern wirst du gefragt, ob überschrieben werden soll.';
    if (tab === 'patterns') {
        const count = $('seg-count');
        if (document.activeElement !== count) count.value = String(d.preset.segments.length);
        $('total-len').textContent = formatNumber(totalLengthCm(d.preset.segments) / 100, 2) + ' m';   /* E-N3 */
    }
}

/* ---- ground view (ADR 0017) ---------------------------------------------------------------- */

const ground = { pxPerCm: null, starts: [] };   /* null = fit to width */

function groundScale() {
    const d = docs.patterns;
    const width = $('ground-scroll').clientWidth;
    const total = totalLengthCm(d.preset.segments);
    return ground.pxPerCm ?? width / total;
}

function renderGround() {
    const d = docs.patterns;
    const scroll = $('ground-scroll');
    const width = scroll.clientWidth;
    if (width === 0) return;
    const segs = d.preset.segments;
    const total = totalLengthCm(segs);
    const scale = groundScale();
    $('ground-spacer').style.width = Math.max(width, total * scale) + 'px';
    const canvas = $('ground-canvas');
    const x0 = scroll.scrollLeft;
    canvas.style.left = x0 + 'px';   /* the canvas covers only the visible part */
    const ctx = fitCanvas(canvas, width, 80, true);
    ctx.clearRect(0, 0, width, 80);
    const band = 52;
    ground.starts = [];
    let pos = 0;
    for (const s of segs) {
        ground.starts.push(pos);
        pos += s.length_cm;
    }
    const sel = new Set(d.selection);
    const border = cssVar('--border');
    for (let i = 0; i < segs.length; i++) {
        const a = ground.starts[i] * scale - x0;
        const b = (ground.starts[i] + segs[i].length_cm) * scale - x0;
        if (b < 0 || a > width) continue;
        ctx.fillStyle = displayColor(segs[i]);
        ctx.fillRect(a, 4, Math.max(b - a, 1), band);
        if (b - a >= 4) {   /* E-C2: thin outline between segments */
            ctx.fillStyle = border;
            ctx.fillRect(b - 0.5, 4, 1, band);
        }
    }
    ctx.strokeStyle = border;
    ctx.lineWidth = 1;
    ctx.strokeRect(-x0 + 0.5, 4.5, total * scale - 1, band - 1);
    /* selection (E-C3) */
    ctx.strokeStyle = cssVar('--accent');
    ctx.lineWidth = 2;
    for (const i of sel) {
        const a = ground.starts[i] * scale - x0;
        const w = segs[i].length_cm * scale;
        if (a + w < 0 || a > width) continue;
        ctx.strokeRect(a + 1, 3, Math.max(w - 2, 1), band + 2);
    }
    /* ruler in m */
    const steps = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000];
    const step = steps.find((s) => s * scale >= 72) ?? 100000;
    ctx.fillStyle = cssVar('--muted');
    ctx.font = '13px Montserrat, system-ui, sans-serif';
    ctx.textBaseline = 'top';
    for (let cm = Math.floor(x0 / scale / step) * step; cm <= total && cm * scale - x0 <= width; cm += step) {
        const x = cm * scale - x0;
        ctx.fillRect(x, band + 6, 1, 5);
        const label = step >= 100 ? formatNumber(cm / 100, 0) + ' m' : formatNumber(cm) + ' cm';
        ctx.fillText(label, x + 3, band + 8);
    }
}

function segmentAt(clientX) {
    const scroll = $('ground-scroll');
    const rect = scroll.getBoundingClientRect();
    const cm = (clientX - rect.left + scroll.scrollLeft) / groundScale();
    const st = ground.starts;
    const segs = docs.patterns.preset.segments;
    if (cm < 0 || cm >= totalLengthCm(segs)) return -1;
    let lo = 0;
    let hi = st.length - 1;
    while (lo < hi) {
        const mid = (lo + hi + 1) >> 1;
        if (st[mid] <= cm) lo = mid; else hi = mid - 1;
    }
    return lo;
}

function zoom(factor) {
    const scroll = $('ground-scroll');
    const old = groundScale();
    const minScale = scroll.clientWidth / totalLengthCm(docs.patterns.preset.segments);
    const next = Math.min(200, Math.max(minScale, old * factor));
    const centerCm = (scroll.scrollLeft + scroll.clientWidth / 2) / old;
    ground.pxPerCm = next <= minScale * 1.0001 ? null : next;
    renderGround();
    scroll.scrollLeft = centerCm * groundScale() - scroll.clientWidth / 2;
    renderGround();
}

/* ---- segment table (virtual rows, E-L4) ------------------------------------------------------ */

const rows = [];

function makeRow() {
    const idx = h('span', { class: 'idx' });
    const len = h('input', { type: 'text', inputmode: 'numeric', class: 'num-input', autocomplete: 'off', 'aria-label': 'Länge in cm' });
    const color = h('input', { type: 'color', class: 'color-input', 'aria-label': 'Farbe' });
    const hex = h('input', { type: 'text', class: 'hex-input', autocomplete: 'off', spellcheck: 'false', 'aria-label': 'Farbe als Hex' });
    const bright = h('input', { type: 'text', inputmode: 'numeric', class: 'num-input', autocomplete: 'off', 'aria-label': 'Helligkeit in %' });
    const row = h('div', { class: 'seg-row', role: 'row' }, idx,
        h('div', { class: 'unit-field' }, len, h('span', {}, 'cm')),
        h('div', { class: 'unit-field' }, color, hex),
        h('div', { class: 'unit-field' }, bright, h('span', {}, '%')));
    row.segIndex = -1;
    const setSeg = (key, value) => {
        const i = row.segIndex;
        change((p) => { p.segments[i][key] = value; return p; }, `row:${i}:${key}`);
    };
    intField(len, { min: LIMITS.minLengthCm, max: LIMITS.maxLengthCm, label: 'Länge', get: () => docs.patterns.preset.segments[row.segIndex]?.length_cm, set: (v) => setSeg('length_cm', v) });
    intField(bright, { min: 0, max: 100, label: 'Helligkeit', get: () => docs.patterns.preset.segments[row.segIndex]?.brightness, set: (v) => setSeg('brightness', v) });
    color.addEventListener('input', () => setSeg('color', color.value));
    hex.addEventListener('change', () => {
        const c = normalizeColor(hex.value);
        hex.classList.toggle('invalid', c === null);
        hex.title = c === null ? 'Farbe als #rrggbb' : '';
        if (c) setSeg('color', c);
    });
    /* selection by click on the row (E-I3); focusing a field selects its row */
    row.addEventListener('mousedown', (e) => {
        if (e.target.tagName === 'INPUT') return;
        e.preventDefault();
        select(row.segIndex, e);
    });
    row.addEventListener('focusin', () => {
        const d = docs.patterns;
        if (!d.selection.includes(row.segIndex)) {
            d.selection = [row.segIndex];
            d.anchor = row.segIndex;
            renderSelectionMarks();
        }
    });
    return row;
}

function select(index, e) {
    const d = docs.patterns;
    const r = clickSelect(d.selection, d.anchor, index, { shift: e.shiftKey, toggle: e.metaKey || e.ctrlKey });
    d.selection = r.selection;
    d.anchor = r.anchor;
    renderSelectionMarks();
}

function renderSelectionMarks() {
    renderGround();
    const sel = new Set(docs.patterns.selection);
    for (const r of rows) r.classList.toggle('sel', sel.has(r.segIndex));
    renderSelection();
}

function renderTable() {
    const d = docs.patterns;
    const body = $('seg-body');
    const segs = d.preset.segments;
    $('seg-spacer').style.height = segs.length * ROW_H + 'px';
    const first = Math.max(0, Math.floor(body.scrollTop / ROW_H) - 2);
    const visible = Math.ceil((body.clientHeight || 400) / ROW_H) + 5;
    while (rows.length < visible) {
        const r = makeRow();
        rows.push(r);
        body.append(r);
    }
    const sel = new Set(d.selection);
    const active = document.activeElement;
    rows.forEach((row, k) => {
        const i = first + k;
        if (i >= segs.length) {
            row.hidden = true;
            row.segIndex = -1;
            return;
        }
        row.hidden = false;
        row.segIndex = i;
        row.style.top = i * ROW_H + 'px';
        row.classList.toggle('sel', sel.has(i));
        const s = segs[i];
        const [idx, lenBox, colorBox, brightBox] = row.children;
        idx.textContent = String(i + 1);
        const len = lenBox.firstChild;
        const [color, hex] = colorBox.children;
        const bright = brightBox.firstChild;
        if (active !== len) { len.value = String(s.length_cm); len.classList.remove('invalid'); }
        if (active !== color) color.value = normalizeColor(s.color) ?? '#000000';
        if (active !== hex) { hex.value = s.color; hex.classList.toggle('invalid', normalizeColor(s.color) === null); }
        if (active !== bright) { bright.value = String(s.brightness); bright.classList.remove('invalid'); }
    });
}

function scrollRowIntoView(i) {
    const body = $('seg-body');
    const top = i * ROW_H;
    if (top < body.scrollTop) body.scrollTop = top;
    else if (top + ROW_H > body.scrollTop + body.clientHeight) body.scrollTop = top + ROW_H - body.clientHeight;
}

/* ---- selection panel (bulk edit) ------------------------------------------------------------- */

function renderSelection() {
    const d = docs.patterns;
    const n = d.preset.segments.length;
    const k = d.selection.length;
    $('sel-info').textContent = k === 0
        ? `Keine Auswahl – Änderungen gelten für alle ${formatNumber(n)} Abschnitte`
        : `${formatNumber(k)} von ${formatNumber(n)} Abschnitten ausgewählt`;
    $('sel-clear').disabled = k === 0;
    $('sel-delete').disabled = k === 0 || k >= n;
    const sum = selectionSummary(d.preset.segments, d.selection);
    const active = document.activeElement;
    const set = (el, v, isColor = false) => {
        if (active === el) return;
        if (v === 'mixed') {
            el.value = isColor ? '#000000' : '';
            el.placeholder = 'gemischt';
        } else {
            el.value = String(v);
            el.placeholder = '';
        }
        el.classList.remove('invalid');
    };
    set($('p-length'), sum.length_cm);
    set($('p-bright'), sum.brightness);
    set($('p-color-hex'), sum.color);
    set($('p-color'), sum.color === 'mixed' ? 'mixed' : (normalizeColor(sum.color) ?? '#000000'), true);
    $('p-err').textContent = '';
}

function bulk(patch, key) {
    change((p) => {
        p.segments = applyToSelection(p.segments, doc().selection, patch);
        return p;
    }, key);
}

/* ---- tools (ADR 0017) ------------------------------------------------------------------------- */

function readInt(id, min, max, label) {
    const el = $(id);
    const v = parseNumber(el.value);
    const okv = Number.isInteger(v) && v >= min && v <= max;
    el.classList.toggle('invalid', !okv);
    if (!okv) throw new Error(`${label}: ${formatNumber(min)} … ${formatNumber(max)}`);
    return v;
}

function runTool(fn) {
    $('t-err').textContent = '';
    try {
        change((p) => fn(p));
    } catch (e) {
        $('t-err').textContent = e.message;   /* E-N4: rejected with a message */
        toast(e.message, true);
    }
}

/* colour stops of the gradient tool (2 … 4, ADR 0017) */
const gradientStops = ['#ff0000', '#0040ff'];
const STOP_DEFAULTS = ['#00ff00', '#ffffff', '#ff00ff'];

function renderGradientStops() {
    const box = $('g-stops');
    box.replaceChildren(...gradientStops.flatMap((c, i) => {
        const input = h('input', { type: 'color', class: 'color-input', value: c, 'aria-label': `Farbe ${i + 1}` });
        input.addEventListener('input', () => { gradientStops[i] = input.value; });
        return i === 0 ? [input] : [h('span', {}, '→'), input];
    }));
    $('g-stop-remove').disabled = gradientStops.length <= 2;
    $('g-stop-add').disabled = gradientStops.length >= 4;
}

function updateNthValueField() {
    const prop = $('t-prop').value;
    const isColor = prop === 'color';
    $('t-value').hidden = isColor;
    $('t-value-unit').hidden = isColor;
    $('t-value-color').hidden = !isColor;
    $('t-value-unit').textContent = prop === 'brightness' ? '%' : 'cm';
}

/* ---- overlay fields ---------------------------------------------------------------------------- */

const OVERLAY_FIELDS = [
    { key: 'level_start', label: 'Pegel Anfang', unit: '%', max: 100, step: 1 },
    { key: 'level_mid', label: 'Pegel Mitte', unit: '%', max: 100, step: 1 },
    { key: 'level_end', label: 'Pegel Ende', unit: '%', max: 100, step: 1 },
    { key: 'fade_start_mm', label: 'Übergang Anfang', unit: 'mm', max: LIMITS.stripLengthMm, step: 5 },
    { key: 'fade_end_mm', label: 'Übergang Ende', unit: 'mm', max: LIMITS.stripLengthMm, step: 5 },
];
const ovlInputs = {};

function buildOverlayFields() {
    const box = $('ovl-fields');
    for (const f of OVERLAY_FIELDS) {
        const range = h('input', { type: 'range', min: 0, max: f.max, step: f.step, id: 'o-' + f.key + '-range', 'aria-label': f.label });
        const num = h('input', { type: 'text', inputmode: 'numeric', class: 'num-input', id: 'o-' + f.key, autocomplete: 'off' });
        const err = h('div', { class: 'field-error', role: 'alert' });
        box.append(h('label', { for: 'o-' + f.key }, f.label), range,
            h('div', { class: 'unit-field' }, num, h('span', {}, f.unit)), err);
        const set = (v) => change((p) => { p[f.key] = v; return p; }, 'ovl:' + f.key);
        range.addEventListener('input', () => set(Number(range.value)));
        intField(num, { min: 0, max: f.max, label: f.label, errorEl: err, get: () => docs.overlays.preset[f.key], set });
        ovlInputs[f.key] = { range, num, err };
    }
}

function renderOverlayFields() {
    const d = docs.overlays;
    const errors = validateOverlay(d.preset);
    for (const f of OVERLAY_FIELDS) {
        const { range, num, err } = ovlInputs[f.key];
        const v = d.preset[f.key];
        if (document.activeElement !== range) range.value = String(v);
        if (document.activeElement !== num) num.value = String(v);
        const e = errors.find((x) => x.path === f.key);
        if (document.activeElement !== num || e) {
            err.textContent = e ? e.message : '';
            num.classList.toggle('invalid', !!e);
        }
    }
}

function renderCurve() {
    const canvas = $('curve-canvas');
    const width = canvas.clientWidth;
    if (width === 0) return;
    const height = 160;
    const ctx = fitCanvas(canvas, width, height);
    ctx.clearRect(0, 0, width, height);
    const o = docs.overlays.preset;
    const pad = { l: 48, r: 12, t: 12, b: 24 };
    const w = width - pad.l - pad.r;
    const hgt = height - pad.t - pad.b;
    ctx.font = '13px Montserrat, system-ui, sans-serif';
    ctx.fillStyle = cssVar('--muted');
    ctx.strokeStyle = cssVar('--border');
    ctx.lineWidth = 1;
    ctx.textBaseline = 'middle';
    ctx.textAlign = 'right';
    for (const lv of [0, 50, 100]) {
        const y = pad.t + hgt * (1 - lv / 100) + 0.5;
        ctx.beginPath();
        ctx.moveTo(pad.l, y);
        ctx.lineTo(pad.l + w, y);
        ctx.stroke();
        ctx.fillText(lv + ' %', pad.l - 8, y);
    }
    ctx.textAlign = 'center';
    ctx.textBaseline = 'top';
    for (const mm of w < 400 ? [0, 250, 500] : [0, 100, 200, 300, 400, 500]) {
        ctx.fillText(mm + ' mm', Math.min(Math.max(pad.l + (w * mm) / STRIP.lengthMm, pad.l + 20), pad.l + w - 20), pad.t + hgt + 6);
    }
    if (validateOverlay(o).some((e) => e.path !== 'name')) return;
    /* bars: factor per LED */
    const bw = w / STRIP.leds;
    ctx.fillStyle = cssVar('--border');
    for (let i = 0; i < STRIP.leds; i++) {
        const f = overlayFactor(o, (i + 0.5) * STRIP.pitchMm);
        ctx.fillRect(pad.l + i * bw + 1, pad.t + hgt * (1 - f), Math.max(bw - 2, 1), hgt * f);
    }
    /* curve */
    ctx.strokeStyle = cssVar('--accent');
    ctx.lineWidth = 2;
    ctx.beginPath();
    for (let x = 0; x <= w; x++) {
        const f = overlayFactor(o, (x / w) * STRIP.lengthMm);
        const y = pad.t + hgt * (1 - f);
        if (x === 0) ctx.moveTo(pad.l + x, y); else ctx.lineTo(pad.l + x, y);
    }
    ctx.stroke();
}

/* ---- strip preview (ADR 0017: same rendering as core) -------------------------------------- */

const preview = { playing: true, speed: 1, offsetMm: 0, last: 0, withSlot: { patterns: null, overlays: null } };

/* The "other" preset shown in the preview: overlay for the pattern tab, pattern for the overlay tab */
function previewOtherCat() {
    return tab === 'patterns' ? 'overlays' : 'patterns';
}

function renderPreviewSelect() {
    const other = previewOtherCat();
    const sel = $('preview-with');
    $('preview-with-label').textContent = other === 'overlays' ? 'Overlay' : 'Muster';
    const items = lists[other].filter((e) => e.kind !== 'empty');
    const activeSlot = settings ? (other === 'patterns' ? settings.active_pattern : settings.active_overlay) : null;
    if (preview.withSlot[tab] === null || !items.some((e) => e.slot === preview.withSlot[tab])) {
        preview.withSlot[tab] = items.some((e) => e.slot === activeSlot) ? activeSlot : (items[0]?.slot ?? null);
    }
    const opts = [
        ...(other === 'overlays' ? [h('option', { value: 'none' }, 'ohne')] : []),
        ...(other === 'patterns' ? [h('option', { value: 'open' }, 'offenes Muster')] : []),
        ...items.map((e) => h('option', { value: String(e.slot) }, (e.kind === 'builtin' ? '🔒 ' : '') + e.name)),
    ];
    const key = JSON.stringify(opts.map((o) => o.value + o.textContent));
    if (sel.dataset.key !== key) {
        sel.replaceChildren(...opts);
        sel.dataset.key = key;
    }
    const v = preview.withSlot[tab];
    sel.value = v === 'none' || v === 'open' ? v : v === null ? (other === 'overlays' ? 'none' : 'open') : String(v);
}

async function previewOther() {
    const other = previewOtherCat();
    const v = preview.withSlot[tab];
    if (v === 'none' || v === null) return null;
    if (v === 'open') return docs.patterns.preset;
    const entry = lists[other].find((e) => e.slot === v);
    if (!entry || entry.kind === 'empty') return null;
    const key = `${other}:${v}:${entry.revision}`;
    if (!presetCache.has(key)) {
        presetCache.set(key, null);
        api.get(other, v).then((r) => presetCache.set(key, r.preset)).catch(() => presetCache.delete(key));
    }
    return presetCache.get(key);
}

const fallbackPattern = { segments: [{ length_cm: 10, color: '#ffffff', brightness: 100 }] };

async function drawStrip(now) {
    const dt = preview.last ? Math.min(0.1, (now - preview.last) / 1000) : 0;
    preview.last = now;
    if (preview.playing) preview.offsetMm += preview.speed * 1000 * dt;
    const canvas = $('strip-canvas');
    const width = canvas.clientWidth;
    if (width > 0) {
        const other = await previewOther();
        let pattern;
        let overlay;
        if (tab === 'patterns') {
            pattern = docs.patterns.preset;
            overlay = other;
        } else {
            pattern = other ?? fallbackPattern;
            overlay = docs.overlays.preset;
        }
        const valid = (pattern === fallbackPattern || validatePattern(pattern).every((e) => e.path === 'name')) &&
            (!overlay || validateOverlay(overlay).every((e) => e.path === 'name'));
        const height = 72;
        const ctx = fitCanvas(canvas, width, height);
        ctx.clearRect(0, 0, width, height);
        if (valid) {
            const total = totalLengthCm(pattern.segments) * 10;
            const offset = ((preview.offsetMm % total) + total) % total;
            const leds = renderStrip(pattern, overlay, offset);
            const pad = 12;
            const pitch = (width - 2 * pad) / STRIP.leds;
            const size = Math.max(2, Math.min(pitch - 2, 18));
            /* LEDs */
            for (let i = 0; i < STRIP.leds; i++) {
                ctx.fillStyle = `rgb(${leds[i].join(',')})`;
                ctx.fillRect(pad + i * pitch + (pitch - size) / 2, 10, size, size);
            }
            ctx.strokeStyle = cssVar('--border');
            ctx.lineWidth = 1;
            ctx.strokeRect(pad - 4.5, 5.5, width - 2 * pad + 8, size + 9);
            /* ground below the strip at the same scale: it stays put, the strip moves */
            const scale = pitch / STRIP.pitchMm;
            const y = 10 + size + 14;
            let x = -offset * scale;
            let i = 0;
            const segs = pattern.segments;
            ctx.save();
            ctx.beginPath();
            ctx.rect(pad, y, width - 2 * pad, height - y - 6);
            ctx.clip();
            while (x < width) {
                const s = segs[i % segs.length];
                const w = s.length_cm * 10 * scale;
                if (x + w > 0) {
                    ctx.fillStyle = displayColor(s);
                    ctx.fillRect(pad + x, y, w, height - y - 6);
                }
                x += w;
                i += 1;
                if (i > 100000) break;
            }
            ctx.restore();
        } else {
            ctx.fillStyle = cssVar('--muted');
            ctx.font = '13px Montserrat, system-ui, sans-serif';
            ctx.textBaseline = 'middle';
            ctx.fillText('Vorschau erst bei gültigen Werten', 16, height / 2);
        }
    }
    requestAnimationFrame(drawStrip);
}

/* ---- tabs and keyboard (E-I2) ------------------------------------------------------------------ */

function setTab(t) {
    tab = t;
    render();
}

function inTextField(e) {
    const t = e.target;
    return t instanceof HTMLInputElement || t instanceof HTMLTextAreaElement || t instanceof HTMLSelectElement;
}

function onKey(e) {
    if ($('dialog').open) return;
    const mod = e.metaKey || e.ctrlKey;
    if (mod && e.key.toLowerCase() === 's') {
        e.preventDefault();
        document.activeElement?.dispatchEvent?.(new Event('change', { bubbles: true }));
        save();
    } else if (mod && e.key.toLowerCase() === 'z' && !inTextField(e)) {
        e.preventDefault();
        undo(e.shiftKey);
    } else if (mod && e.key.toLowerCase() === 'a' && !inTextField(e) && tab === 'patterns') {
        e.preventDefault();
        const d = doc();
        d.selection = d.preset.segments.map((_, i) => i);
        renderSelectionMarks();
    } else if (e.key === 'Escape' && !inTextField(e)) {
        if (document.body.classList.contains('drawer-open')) { document.body.classList.remove('drawer-open'); return; }
        if (tab === 'patterns') { doc().selection = []; renderSelectionMarks(); }
    } else if ((e.key === 'Delete' || e.key === 'Backspace') && !inTextField(e) && tab === 'patterns') {
        e.preventDefault();
        deleteSelected();
    }
}

function deleteSelected() {
    const d = docs.patterns;
    if (d.selection.length === 0) return;
    if (d.selection.length >= d.preset.segments.length) {
        toast('Mindestens ein Abschnitt muss bleiben', true);
        return;
    }
    const n = d.selection.length;
    change((p) => ({ preset: { ...p, segments: deleteSegments(p.segments, d.selection) }, selection: [] }));
    toast(`${n} ${n === 1 ? 'Abschnitt' : 'Abschnitte'} gelöscht`);
}

/* ---- wiring ------------------------------------------------------------------------------------- */

function wire() {
    for (const c of CATS) $('tab-' + c).addEventListener('click', () => setTab(c));
    $('drawer-toggle').addEventListener('click', () => {
        const open = document.body.classList.toggle('drawer-open');
        $('drawer-toggle').setAttribute('aria-expanded', String(open));
    });
    $('undo').addEventListener('click', () => undo(false));
    $('redo').addEventListener('click', () => undo(true));
    $('save').addEventListener('click', save);
    $('try').addEventListener('click', tryOnStrip);
    $('notice-reload').addEventListener('click', () => reloadDoc());
    $('act-new').addEventListener('click', newDoc);
    $('act-dup').addEventListener('click', duplicateDoc);
    $('act-del').addEventListener('click', deletePreset);
    $('act-export').addEventListener('click', exportDoc);
    $('act-export-all').addEventListener('click', exportAll);
    $('act-import').addEventListener('click', () => $('file-input').click());
    $('file-input').addEventListener('change', async (e) => {
        await importFiles([...e.target.files]);
        e.target.value = '';
    });

    $('name').addEventListener('input', () => {
        const v = $('name').value;
        change((p) => { p.name = v; return p; }, 'name');
    });

    /* number of segments (ADR 0017) */
    const count = $('seg-count');
    intField(count, {
        min: 1, max: LIMITS.maxSegments, label: 'Abschnitte',
        get: () => docs.patterns.preset.segments.length,
        set: (v) => change((p) => { p.segments = setSegmentCount(p.segments, v); return p; }, 'count'),
    });
    const stepCount = (delta) => change((p) => { p.segments = setSegmentCount(p.segments, p.segments.length + delta); return p; }, 'count');
    $('seg-minus').addEventListener('click', (e) => stepCount(e.shiftKey ? -10 : -1));
    $('seg-plus').addEventListener('click', (e) => stepCount(e.shiftKey ? 10 : 1));

    /* ground view */
    const scroll = $('ground-scroll');
    scroll.addEventListener('scroll', renderGround);
    const canvas = $('ground-canvas');
    canvas.addEventListener('mousedown', (e) => {
        const i = segmentAt(e.clientX);
        if (i < 0) return;
        e.preventDefault();
        select(i, e);
        scrollRowIntoView(i);
        renderTable();
    });
    canvas.addEventListener('mousemove', (e) => {   /* E-I4 */
        const i = segmentAt(e.clientX);
        const tip = $('tooltip');
        if (i < 0) { tip.hidden = true; return; }
        const s = docs.patterns.preset.segments[i];
        tip.textContent = `#${i + 1} · ${formatNumber(s.length_cm)} cm · ${s.color} · ${s.brightness} %`;
        const host = $('ground').getBoundingClientRect();
        tip.hidden = false;
        tip.style.left = Math.min(e.clientX - host.left + 12, host.width - tip.offsetWidth - 8) + 'px';
        tip.style.top = e.clientY - host.top + 16 + 'px';
    });
    canvas.addEventListener('mouseleave', () => { $('tooltip').hidden = true; });
    $('zoom-fit').addEventListener('click', () => { ground.pxPerCm = null; renderGround(); });
    $('zoom-in').addEventListener('click', () => zoom(2));
    $('zoom-out').addEventListener('click', () => zoom(0.5));

    /* table */
    $('seg-body').addEventListener('scroll', renderTable);

    /* selection panel */
    intField($('p-length'), { min: LIMITS.minLengthCm, max: LIMITS.maxLengthCm, label: 'Länge', errorEl: $('p-err'), get: () => null, set: (v) => bulk({ length_cm: v }, 'bulk:length') });
    intField($('p-bright'), { min: 0, max: 100, label: 'Helligkeit', errorEl: $('p-err'), get: () => null, set: (v) => bulk({ brightness: v }, 'bulk:brightness') });
    $('p-color').addEventListener('input', () => bulk({ color: $('p-color').value }, 'bulk:color'));
    $('p-color-hex').addEventListener('change', () => {
        const el = $('p-color-hex');
        if (el.value.trim() === '') return;
        const c = normalizeColor(el.value);
        el.classList.toggle('invalid', c === null);
        $('p-err').textContent = c === null ? 'Farbe als #rrggbb' : '';
        if (c) bulk({ color: c }, 'bulk:color');
    });
    $('sel-clear').addEventListener('click', () => { docs.patterns.selection = []; renderSelectionMarks(); });
    $('sel-delete').addEventListener('click', deleteSelected);

    /* tools */
    $('t-prop').addEventListener('change', () => {
        $('t-value').value = $('t-prop').value === 'brightness' ? '0' : '10';
        updateNthValueField();
    });
    $('t-nth-apply').addEventListener('click', () => runTool((p) => {
        const n = readInt('t-n', 2, LIMITS.maxSegments, 'n');
        const start = readInt('t-start', 1, n, 'Start');
        const prop = $('t-prop').value;
        const value = prop === 'color' ? $('t-value-color').value
            : prop === 'brightness' ? readInt('t-value', 0, 100, 'Helligkeit')
                : readInt('t-value', LIMITS.minLengthCm, LIMITS.maxLengthCm, 'Länge');
        p.segments = toolEveryNth(p.segments, doc().selection, { n, start, prop, value });
        return p;
    }));
    $('g-stop-add').addEventListener('click', () => {
        if (gradientStops.length < 4) gradientStops.push(STOP_DEFAULTS[gradientStops.length - 2]);
        renderGradientStops();
    });
    $('g-stop-remove').addEventListener('click', () => {
        if (gradientStops.length > 2) gradientStops.pop();
        renderGradientStops();
    });
    $('t-grad-apply').addEventListener('click', () => runTool((p) => {
        const color = $('g-color-on').checked
            ? { stops: gradientStops.slice(), cyclic: $('g-cyclic').checked } : null;
        const brightness = $('g-bright-on').checked
            ? { from: readInt('g-b-from', 0, 100, 'Helligkeit'), to: readInt('g-b-to', 0, 100, 'Helligkeit') } : null;
        if (!color && !brightness) throw new Error('Farbe oder Helligkeit wählen');
        p.segments = toolGradient(p.segments, doc().selection, { color, brightness });
        return p;
    }));
    $('t-rep-apply').addEventListener('click', () => runTool((p) => {
        const times = readInt('r-times', 1, LIMITS.maxSegments, 'Anzahl');
        const r = toolRepeatGroup(p.segments, doc().selection, times);
        return { preset: { ...p, segments: r.segments }, selection: r.selection };
    }));
    $('t-len-apply').addEventListener('click', () => runTool((p) => {
        const mode = $('l-mode').value;
        const value = readInt('l-value', 1, mode === 'total' ? LIMITS.maxSegments * LIMITS.maxLengthCm : LIMITS.maxLengthCm, 'Länge');
        p.segments = toolDistributeLength(p.segments, doc().selection, { mode, value });
        return p;
    }));
    for (const id of ['t-n', 't-start', 't-value', 'g-b-from', 'g-b-to', 'r-times', 'l-value']) {
        $(id).addEventListener('input', () => $(id).classList.remove('invalid'));
    }

    /* preview */
    $('play').addEventListener('click', () => {
        preview.playing = !preview.playing;
        $('play').textContent = preview.playing ? '❚❚' : '▶';
        $('play').setAttribute('aria-label', preview.playing ? 'Fahrt anhalten' : 'Fahrt starten');
    });
    $('speed').addEventListener('input', () => {
        preview.speed = Number($('speed').value);
        $('speed-out').textContent = formatNumber(preview.speed, 1) + ' m/s';
    });
    $('preview-with').addEventListener('change', () => {
        const v = $('preview-with').value;
        preview.withSlot[tab] = v === 'none' || v === 'open' ? v : Number(v);
    });

    document.addEventListener('keydown', onKey);
    window.addEventListener('resize', () => render());
    window.addEventListener('beforeunload', (e) => {   /* E-S2 */
        if (CATS.some((c) => docs[c] && isDirty(docs[c]) && (docs[c].kind !== null || docs[c].undo.canUndo()))) {
            e.preventDefault();
            e.returnValue = '';
        }
    });
}

/* ---- start --------------------------------------------------------------------------------------- */

async function start() {
    buildOverlayFields();
    updateNthValueField();
    renderGradientStops();
    wire();
    api = await connect();
    setConn(api.mode === 'dash' ? 'ok' : 'local');
    if (api.mode === 'dash') {
        api.setTime().catch(() => {});   /* ADR 0014: browser time when the editor opens */
    }
    await refreshState();
    for (const cat of CATS) {
        const first = lists[cat].find((e) => e.kind !== 'empty');
        const activeSlot = settings ? (cat === 'patterns' ? settings.active_pattern : settings.active_overlay) : null;
        const slot = lists[cat].some((e) => e.slot === activeSlot && e.kind !== 'empty') ? activeSlot : first?.slot;
        if (slot !== undefined) {
            try {
                const r = await api.get(cat, slot);
                docs[cat] = makeDoc(cat, r.preset, { slot, kind: r.builtin ? 'builtin' : 'user', revision: r.revision });
                continue;
            } catch {
                /* fall through to a new preset */
            }
        }
        docs[cat] = makeDoc(cat, cat === 'patterns' ? newPattern() : newOverlay());
    }
    render();
    requestAnimationFrame(drawStrip);
    setInterval(refreshState, POLL_MS);
}

start();
