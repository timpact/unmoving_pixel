/*
 * Preset editor – data model, validation, tools and undo (ADR 0011, 0012, 0017).
 *
 * Pure functions without DOM access, so they run in the browser and in Node tests.
 * Functions never modify their arguments; they return new arrays/objects.
 *
 * Pattern JSON (ADR 0013):
 *   { format_version: 1, type: "pattern", name,
 *     segments: [{ length_cm, color: "#rrggbb", brightness }] }
 * Overlay JSON:
 *   { format_version: 1, type: "overlay", overlay_type: "edge_fade", name,
 *     level_start, level_mid, level_end, fade_start_mm, fade_end_mm }
 */

export const LIMITS = Object.freeze({
    maxSegments: 2048,
    minLengthCm: 1,
    maxLengthCm: 65535,
    nameBytes: 16,
    fadeMaxMm: 1000,      /* overlay fades: maximum main part length (ADR 0012, 0018) */
});

/* ---- text and numbers ------------------------------------------------------ */

export function utf8Length(str) {
    return new TextEncoder().encode(str).length;
}

/* Shortens a string to at most `bytes` UTF-8 bytes without cutting a character */
export function truncateUtf8(str, bytes) {
    let out = '';
    for (const ch of str) {
        if (utf8Length(out + ch) > bytes) break;
        out += ch;
    }
    return out;
}

/* Accepts "12,5" and "12.5"; returns NaN for anything else (E-N1, E-N4) */
export function parseNumber(text) {
    const t = String(text).trim().replace(',', '.');
    if (!/^-?\d+(\.\d+)?$/.test(t)) return NaN;
    return Number(t);
}

/* German format with thousands separator (N1), e.g. formatNumber(1234.5, 1) → "1.234,5" */
export function formatNumber(value, decimals = 0) {
    const neg = value < 0;
    const fixed = Math.abs(value).toFixed(decimals);
    const [int, frac] = fixed.split('.');
    const grouped = int.replace(/\B(?=(\d{3})+(?!\d))/g, '.');
    return (neg ? '-' : '') + grouped + (frac ? ',' + frac : '');
}

export function normalizeColor(text) {
    const m = /^#?([0-9a-f]{6})$/i.exec(String(text).trim());
    return m ? '#' + m[1].toLowerCase() : null;
}

/* ---- creation ------------------------------------------------------------------ */

export function newSegment(overrides = {}) {
    return { length_cm: 10, color: '#2dd4bf', brightness: 100, ...overrides };
}

export function newPattern(name = 'Neues Muster') {
    return {
        format_version: 1,
        type: 'pattern',
        name,
        segments: [newSegment(), newSegment({ color: '#000000', brightness: 0 })],
    };
}

export function newOverlay(name = 'Neues Overlay') {
    return {
        format_version: 1,
        type: 'overlay',
        overlay_type: 'edge_fade',
        name,
        level_start: 20,
        level_mid: 100,
        level_end: 20,
        fade_start_mm: 150,
        fade_end_mm: 150,
    };
}

/* ---- validation ------------------------------------------------------------------ */

function nameErrors(name) {
    const errors = [];
    if (typeof name !== 'string' || name.trim() === '') {
        errors.push({ path: 'name', message: 'Name fehlt' });
    } else if (utf8Length(name) > LIMITS.nameBytes) {
        errors.push({ path: 'name', message: `Name zu lang (max. ${LIMITS.nameBytes} Byte)` });
    }
    return errors;
}

function isInt(v, min, max) {
    return Number.isInteger(v) && v >= min && v <= max;
}

/* Returns a list of { path, message }; empty if the pattern is valid (ADR 0011) */
export function validatePattern(p) {
    const errors = nameErrors(p?.name);
    const segs = p?.segments;
    if (!Array.isArray(segs) || segs.length < 1) {
        errors.push({ path: 'segments', message: 'Mindestens ein Abschnitt nötig' });
        return errors;
    }
    if (segs.length > LIMITS.maxSegments) {
        errors.push({ path: 'segments', message: `Höchstens ${LIMITS.maxSegments} Abschnitte` });
    }
    segs.forEach((s, i) => {
        if (!isInt(s.length_cm, LIMITS.minLengthCm, LIMITS.maxLengthCm)) {
            errors.push({ path: `segments[${i}].length_cm`, message: `Länge ${LIMITS.minLengthCm} … ${LIMITS.maxLengthCm} cm` });
        }
        if (normalizeColor(s.color) === null) {
            errors.push({ path: `segments[${i}].color`, message: 'Farbe als #rrggbb' });
        }
        if (!isInt(s.brightness, 0, 100)) {
            errors.push({ path: `segments[${i}].brightness`, message: 'Helligkeit 0 … 100 %' });
        }
    });
    return errors;
}

/*
 * ADR 0012/0018: levels 0 … 100 %, fades 0 … 1000 mm and together at most 1000 mm
 * (maximum main part length). On a shorter strip both fades are scaled down.
 */
export function validateOverlay(o, maxMm = LIMITS.fadeMaxMm) {
    const errors = nameErrors(o?.name);
    for (const k of ['level_start', 'level_mid', 'level_end']) {
        if (!isInt(o?.[k], 0, 100)) errors.push({ path: k, message: 'Pegel 0 … 100 %' });
    }
    for (const k of ['fade_start_mm', 'fade_end_mm']) {
        if (!isInt(o?.[k], 0, maxMm)) errors.push({ path: k, message: `Übergang 0 … ${formatNumber(maxMm)} mm` });
    }
    if (isInt(o?.fade_start_mm, 0, 65535) && isInt(o?.fade_end_mm, 0, 65535) &&
        o.fade_start_mm + o.fade_end_mm > maxMm) {
        errors.push({ path: 'fade_end_mm', message: `Übergänge zusammen max. ${formatNumber(maxMm)} mm` });
    }
    return errors;
}

/* ---- JSON ---------------------------------------------------------------------------- */

/* Parses and validates a preset file; throws an Error with a German message */
export function parsePreset(text) {
    let obj;
    try {
        obj = JSON.parse(text);
    } catch {
        throw new Error('Keine gültige JSON-Datei');
    }
    if (obj?.format_version !== 1) throw new Error('Unbekannte Formatversion');
    let errors;
    if (obj.type === 'pattern') {
        obj = {
            format_version: 1, type: 'pattern', name: obj.name,
            segments: (obj.segments ?? []).map((s) => ({
                length_cm: s.length_cm, color: normalizeColor(s.color) ?? s.color, brightness: s.brightness,
            })),
        };
        errors = validatePattern(obj);
    } else if (obj.type === 'overlay') {
        errors = validateOverlay(obj);
    } else {
        throw new Error('Unbekannter Preset-Typ');
    }
    if (errors.length > 0) {
        throw new Error(errors.map((e) => `${e.path}: ${e.message}`).join('; '));
    }
    return obj;
}

export function serializePreset(preset) {
    return JSON.stringify(preset, null, 2) + '\n';
}

/*
 * Bundle of several presets (export "all", ADR 0013):
 *   { format_version: 1, type: "bundle", presets: [ ...pattern/overlay JSON ] }
 */
export function makeBundle(presets) {
    return { format_version: 1, type: 'bundle', presets };
}

/* Parses a single preset or a bundle; returns an array of validated presets */
export function parsePresetFile(text) {
    let obj;
    try {
        obj = JSON.parse(text);
    } catch {
        throw new Error('Keine gültige JSON-Datei');
    }
    if (obj?.type !== 'bundle') return [parsePreset(text)];
    if (obj.format_version !== 1) throw new Error('Unbekannte Formatversion');
    if (!Array.isArray(obj.presets)) throw new Error('Sammeldatei ohne Presets');
    return obj.presets.map((p, i) => {
        try {
            return parsePreset(JSON.stringify(p));
        } catch (e) {
            throw new Error(`Preset ${i + 1}: ${e.message}`);
        }
    });
}

/* File name for an exported preset, e.g. "Weiche Ränder" → "weiche-raender.json" */
export function presetFileName(name) {
    const map = { ä: 'ae', ö: 'oe', ü: 'ue', ß: 'ss' };
    const slug = name.toLowerCase().replace(/[äöüß]/g, (c) => map[c])
        .replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '');
    return (slug || 'preset') + '.json';
}

/*
 * Name for a copy (ADR 0017): "<name> (2)" or the next free number, shortened to
 * 16 bytes UTF-8. An existing " (n)" suffix is replaced, not repeated.
 */
export function copyName(name, existingNames) {
    const taken = new Set(existingNames);
    const base = name.replace(/ \(\d+\)$/, '');
    for (let n = 2; ; n++) {
        const suffix = ` (${n})`;
        const head = truncateUtf8(base, LIMITS.nameBytes - utf8Length(suffix)).trimEnd();
        const candidate = head + suffix;
        if (!taken.has(candidate)) return candidate;
    }
}

/* ---- segments ------------------------------------------------------------------------ */

export function totalLengthCm(segments) {
    return segments.reduce((sum, s) => sum + s.length_cm, 0);
}

/* Changes the number of segments: appends copies of the last, or removes from the end */
export function setSegmentCount(segments, count) {
    const n = Math.max(1, Math.min(LIMITS.maxSegments, Math.round(count)));
    if (n <= segments.length) return segments.slice(0, n);
    const last = segments[segments.length - 1] ?? newSegment();
    return segments.concat(Array.from({ length: n - segments.length }, () => ({ ...last })));
}

/* Deletes the selected segments; at least one segment remains */
export function deleteSegments(segments, indices) {
    const del = new Set(indices);
    const rest = segments.filter((_, i) => !del.has(i));
    return rest.length > 0 ? rest : [segments[0]];
}

/* Sorted, unique, in-range indices; an empty selection means "all segments" */
export function effectiveSelection(segments, indices) {
    const sel = [...new Set(indices)].filter((i) => i >= 0 && i < segments.length).sort((a, b) => a - b);
    return sel.length > 0 ? sel : segments.map((_, i) => i);
}

/* Bulk edit: sets the given properties for all selected segments */
export function applyToSelection(segments, indices, patch) {
    const sel = new Set(effectiveSelection(segments, indices));
    return segments.map((s, i) => (sel.has(i) ? { ...s, ...patch } : s));
}

/* Values of the selection; differing values are reported as "mixed" */
export function selectionSummary(segments, indices) {
    const sel = effectiveSelection(segments, indices);
    const out = {};
    for (const key of ['length_cm', 'color', 'brightness']) {
        const values = new Set(sel.map((i) => segments[i][key]));
        out[key] = values.size === 1 ? [...values][0] : 'mixed';
    }
    return out;
}

/* ---- selection (E-I3) ------------------------------------------------------------- */

/*
 * Click handling: plain click selects one, shift selects the range from the anchor,
 * toggle (Cmd/Ctrl) adds or removes one. Returns { selection, anchor }.
 */
export function clickSelect(selection, anchor, index, { shift = false, toggle = false } = {}) {
    if (shift && anchor !== null && anchor !== undefined) {
        const [a, b] = anchor < index ? [anchor, index] : [index, anchor];
        const range = Array.from({ length: b - a + 1 }, (_, k) => a + k);
        const base = toggle ? selection : [];
        return { selection: [...new Set([...base, ...range])].sort((x, y) => x - y), anchor };
    }
    if (toggle) {
        const has = selection.includes(index);
        const next = has ? selection.filter((i) => i !== index) : [...selection, index].sort((x, y) => x - y);
        return { selection: next, anchor: index };
    }
    return { selection: [index], anchor: index };
}

/* ---- tools (ADR 0017) ------------------------------------------------------------- */

/*
 * Every n-th: within the selection (in order), every n-th segment starting at
 * position `start` (1-based) gets `prop` = `value`.
 * Example: n = 2, start = 2 → 2nd, 4th, 6th, … selected segment.
 */
export function toolEveryNth(segments, indices, { n, start, prop, value }) {
    if (!Number.isInteger(n) || n < 2) throw new Error('n muss mindestens 2 sein');
    if (!Number.isInteger(start) || start < 1 || start > n) throw new Error(`Start muss 1 … ${n} sein`);
    if (!['length_cm', 'color', 'brightness'].includes(prop)) throw new Error('Unbekannte Eigenschaft');
    const sel = effectiveSelection(segments, indices);
    const hit = new Set(sel.filter((_, pos) => (pos + 1 - start) >= 0 && (pos + 1 - start) % n === 0));
    return segments.map((s, i) => (hit.has(i) ? { ...s, [prop]: value } : s));
}

/* sRGB transfer function (IEC 61966-2-1); shared with render.js */
export function srgbToLinear(c) {
    return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
}

export function linearToSrgb(c) {
    return c <= 0.0031308 ? c * 12.92 : 1.055 * c ** (1 / 2.4) - 0.055;
}

export function hexToRgb(hex) {
    const v = parseInt(normalizeColor(hex).slice(1), 16);
    return [(v >> 16) & 255, (v >> 8) & 255, v & 255];
}

export function rgbToHex([r, g, b]) {
    return '#' + [r, g, b].map((x) => Math.round(Math.min(255, Math.max(0, x))).toString(16).padStart(2, '0')).join('');
}

/* Colour between a and b at t (0 … 1), interpolated in linear light */
export function mixColor(a, b, t) {
    const ca = hexToRgb(a).map((x) => srgbToLinear(x / 255));
    const cb = hexToRgb(b).map((x) => srgbToLinear(x / 255));
    return rgbToHex(ca.map((x, k) => linearToSrgb(x + (cb[k] - x) * t) * 255));
}

/*
 * Colour at t (0 … 1) of a multi-colour gradient with evenly spaced stops.
 *   open:   stops at t = 0, 1/(k-1), …, 1 – the last stop is reached at t = 1
 *   cyclic: stops at t = 0, 1/k, …, (k-1)/k, then back to the first stop at
 *           t = 1, so a repeating pattern continues without a seam
 */
export function gradientColor(stops, t, cyclic = false) {
    const k = stops.length;
    if (k === 1) return normalizeColor(stops[0]);
    const spans = cyclic ? k : k - 1;
    const u = Math.min(Math.max(t, 0), 1) * spans;
    const i = Math.min(Math.floor(u), spans - 1);
    return mixColor(stops[i], stops[(i + 1) % k], u - i);
}

/*
 * Gradient over the selection (in order, evenly by segment position):
 *   color:      { stops: [2 … 4 colours], cyclic }  (ADR 0017)
 *   brightness: { from, to }, linear
 * Open gradient: the first selected segment gets the first colour, the last one
 * the last colour. Cyclic: the colours are spread over the selection as one
 * period; the segment after the selection would get the first colour again.
 */
export function toolGradient(segments, indices, { color = null, brightness = null }) {
    if (color && (!Array.isArray(color.stops) || color.stops.length < 2 || color.stops.length > 4 ||
        color.stops.some((c) => normalizeColor(c) === null))) {
        throw new Error('Verlauf braucht 2 … 4 Farben');
    }
    const sel = effectiveSelection(segments, indices);
    const n = sel.length;
    const out = segments.slice();
    sel.forEach((idx, pos) => {
        const s = { ...out[idx] };
        if (color) {
            const t = color.cyclic ? pos / n : (n > 1 ? pos / (n - 1) : 0);
            s.color = gradientColor(color.stops, t, !!color.cyclic);
        }
        if (brightness) {
            const t = n > 1 ? pos / (n - 1) : 0;
            s.brightness = Math.round(brightness.from + (brightness.to - brightness.from) * t);
        }
        out[idx] = s;
    });
    return out;
}

/*
 * Repeat group: the selected contiguous range is inserted `times` times directly
 * after itself. Returns { segments, selection } with the new copies selected.
 */
export function toolRepeatGroup(segments, indices, times) {
    if (!Number.isInteger(times) || times < 1) throw new Error('Anzahl muss mindestens 1 sein');
    const sel = effectiveSelection(segments, indices);
    const first = sel[0];
    const last = sel[sel.length - 1];
    if (last - first + 1 !== sel.length) throw new Error('Die Auswahl muss zusammenhängend sein');
    const group = segments.slice(first, last + 1);
    if (segments.length + group.length * times > LIMITS.maxSegments) {
        throw new Error(`Mehr als ${LIMITS.maxSegments} Abschnitte`);
    }
    const copies = Array.from({ length: times }, () => group.map((s) => ({ ...s }))).flat();
    const result = [...segments.slice(0, last + 1), ...copies, ...segments.slice(last + 1)];
    const selection = Array.from({ length: copies.length }, (_, k) => last + 1 + k);
    return { segments: result, selection };
}

/*
 * Distribute length over the selection:
 *   mode "each":  every segment gets `value` cm
 *   mode "total": `value` cm split evenly, the remainder goes to the first segments
 */
export function toolDistributeLength(segments, indices, { mode, value }) {
    const sel = effectiveSelection(segments, indices);
    if (!Number.isInteger(value)) throw new Error('Länge muss eine ganze Zahl sein');
    let lengths;
    if (mode === 'each') {
        lengths = sel.map(() => value);
    } else if (mode === 'total') {
        const base = Math.floor(value / sel.length);
        const rest = value - base * sel.length;
        lengths = sel.map((_, pos) => base + (pos < rest ? 1 : 0));
    } else {
        throw new Error('Unbekannter Modus');
    }
    if (lengths.some((l) => l < LIMITS.minLengthCm || l > LIMITS.maxLengthCm)) {
        throw new Error(`Jeder Abschnitt muss ${LIMITS.minLengthCm} … ${LIMITS.maxLengthCm} cm lang sein`);
    }
    const out = segments.slice();
    sel.forEach((idx, pos) => {
        out[idx] = { ...out[idx], length_cm: lengths[pos] };
    });
    return out;
}

/* ---- undo (ADR 0017: at least 100 steps) -------------------------------------------- */

export class UndoStack {
    constructor(limit = 200) {
        this.limit = limit;
        this.past = [];
        this.future = [];
    }

    /* Call with the state *before* a change */
    push(state) {
        this.past.push(structuredClone(state));
        if (this.past.length > this.limit) this.past.shift();
        this.future = [];
    }

    canUndo() {
        return this.past.length > 0;
    }

    canRedo() {
        return this.future.length > 0;
    }

    /* Returns the previous state; `current` is kept for redo */
    undo(current) {
        if (!this.canUndo()) return current;
        this.future.push(structuredClone(current));
        return this.past.pop();
    }

    redo(current) {
        if (!this.canRedo()) return current;
        this.past.push(structuredClone(current));
        return this.future.pop();
    }

    clear() {
        this.past = [];
        this.future = [];
    }
}
