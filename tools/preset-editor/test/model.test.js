/* Tests for model.js – run with: node --test tools/preset-editor/test/ */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import {
    LIMITS, UndoStack, applyToSelection, clickSelect, copyName, deleteSegments, formatNumber, makeBundle, mixColor,
    newOverlay, newPattern, parseNumber, parsePreset, parsePresetFile, presetFileName, selectionSummary, serializePreset,
    setSegmentCount, toolDistributeLength, toolEveryNth, toolGradient, toolRepeatGroup,
    totalLengthCm, truncateUtf8, utf8Length, validateOverlay, validatePattern,
} from '../model.js';

const seg = (length_cm, color = '#ff0000', brightness = 100) => ({ length_cm, color, brightness });
const segs = (n) => Array.from({ length: n }, (_, i) => seg(i + 1));

test('numbers: German format and input with comma or point', () => {
    assert.equal(formatNumber(1234.5, 1), '1.234,5');
    assert.equal(formatNumber(-99999.99, 2), '-99.999,99');
    assert.equal(formatNumber(12, 0), '12');
    assert.equal(parseNumber('12,5'), 12.5);
    assert.equal(parseNumber(' 7.25 '), 7.25);
    assert.ok(Number.isNaN(parseNumber('12a')));
    assert.ok(Number.isNaN(parseNumber('')));
});

test('name: UTF-8 byte limit of 16', () => {
    assert.equal(utf8Length('Ränder'), 7);
    assert.equal(truncateUtf8('Weiche Ränder Spezial', 16), 'Weiche Ränder S');
    assert.equal(utf8Length(truncateUtf8('ääääääääää', 16)), 16);
    assert.deepEqual(validatePattern({ ...newPattern(), name: 'x'.repeat(17) }).map((e) => e.path), ['name']);
    assert.deepEqual(validatePattern({ ...newPattern(), name: '  ' }).map((e) => e.path), ['name']);
});

test('validation of patterns (ADR 0011 limits)', () => {
    assert.deepEqual(validatePattern(newPattern()), []);
    const bad = { name: 'x', segments: [seg(0), seg(65536), seg(10, 'red'), seg(10, '#000000', 101)] };
    assert.deepEqual(validatePattern(bad).map((e) => e.path), [
        'segments[0].length_cm', 'segments[1].length_cm', 'segments[2].color', 'segments[3].brightness',
    ]);
    assert.equal(validatePattern({ name: 'x', segments: [] }).length, 1);
    assert.equal(validatePattern({ name: 'x', segments: segs(LIMITS.maxSegments + 1).map(() => seg(1)) }).length, 1);
});

test('validation of overlays (ADR 0012)', () => {
    assert.deepEqual(validateOverlay(newOverlay()), []);
    assert.deepEqual(validateOverlay({ ...newOverlay(), level_mid: 120 }).map((e) => e.path), ['level_mid']);
    /* fades up to 1000 mm together (maximum main part length, ADR 0018) */
    assert.deepEqual(validateOverlay({ ...newOverlay(), fade_start_mm: 300, fade_end_mm: 300 }), []);
    assert.deepEqual(validateOverlay({ ...newOverlay(), fade_start_mm: 600, fade_end_mm: 600 }).map((e) => e.path),
        ['fade_end_mm']);
    assert.deepEqual(validateOverlay({ ...newOverlay(), fade_start_mm: 1001 }).map((e) => e.path),
        ['fade_start_mm', 'fade_end_mm']);
});

test('segment count: append copies of the last, remove from the end, limits', () => {
    const s = [seg(5, '#00ff00', 50), seg(7, '#0000ff', 30)];
    const more = setSegmentCount(s, 4);
    assert.equal(more.length, 4);
    assert.deepEqual(more[3], seg(7, '#0000ff', 30));
    assert.notEqual(more[3], s[1], 'copies are new objects');
    assert.deepEqual(setSegmentCount(more, 1), [s[0]]);
    assert.equal(setSegmentCount(s, 0).length, 1);
    assert.equal(setSegmentCount(s, 5000).length, LIMITS.maxSegments);
    assert.equal(totalLengthCm(more), 5 + 7 + 7 + 7);
    assert.equal(s.length, 2, 'input unchanged');
});

test('delete keeps at least one segment', () => {
    assert.deepEqual(deleteSegments(segs(3), [0, 2]).map((x) => x.length_cm), [2]);
    assert.equal(deleteSegments(segs(2), [0, 1]).length, 1);
});

test('selection: click, shift range, toggle', () => {
    let st = clickSelect([], null, 3);
    assert.deepEqual(st, { selection: [3], anchor: 3 });
    st = clickSelect(st.selection, st.anchor, 6, { shift: true });
    assert.deepEqual(st.selection, [3, 4, 5, 6]);
    st = clickSelect(st.selection, st.anchor, 5, { toggle: true });
    assert.deepEqual(st.selection, [3, 4, 6]);
    st = clickSelect(st.selection, st.anchor, 1, { shift: true, toggle: true });
    assert.deepEqual(st.selection, [1, 2, 3, 4, 5, 6]);
});

test('bulk edit and summary with "mixed"', () => {
    const s = [seg(5), seg(5, '#00ff00'), seg(8)];
    assert.deepEqual(selectionSummary(s, [0, 2]), { length_cm: 'mixed', color: '#ff0000', brightness: 100 });
    const e = applyToSelection(s, [0, 1], { brightness: 40 });
    assert.deepEqual(e.map((x) => x.brightness), [40, 40, 100]);
    assert.deepEqual(applyToSelection(s, [], { length_cm: 3 }).map((x) => x.length_cm), [3, 3, 3], 'empty = all');
});

test('tool: every n-th (every 2nd from the 2nd: brightness 0)', () => {
    const out = toolEveryNth(segs(6), [], { n: 2, start: 2, prop: 'brightness', value: 0 });
    assert.deepEqual(out.map((x) => x.brightness), [100, 0, 100, 0, 100, 0]);
    const sel = toolEveryNth(segs(9), [3, 4, 5, 6, 7, 8], { n: 3, start: 1, prop: 'color', value: '#000000' });
    assert.deepEqual(sel.map((x) => x.color === '#000000'), [false, false, false, true, false, false, true, false, false]);
    assert.throws(() => toolEveryNth(segs(3), [], { n: 1, start: 1, prop: 'brightness', value: 0 }));
    assert.throws(() => toolEveryNth(segs(3), [], { n: 2, start: 3, prop: 'brightness', value: 0 }));
});

test('tool: gradient in linear light', () => {
    const out = toolGradient(segs(3), [], { color: { stops: ['#000000', '#ffffff'] }, brightness: { from: 0, to: 100 } });
    assert.deepEqual(out.map((x) => x.brightness), [0, 50, 100]);
    assert.equal(out[0].color, '#000000');
    assert.equal(out[2].color, '#ffffff');
    assert.equal(out[1].color, '#bcbcbc', '50 % linear light is sRGB 188');
    assert.equal(mixColor('#ff0000', '#ff0000', 0.3), '#ff0000');
});

test('tool: repeat group', () => {
    const s = [seg(1), seg(2), seg(3), seg(4)];
    const { segments, selection } = toolRepeatGroup(s, [1, 2], 2);
    assert.deepEqual(segments.map((x) => x.length_cm), [1, 2, 3, 2, 3, 2, 3, 4]);
    assert.deepEqual(selection, [3, 4, 5, 6]);
    assert.throws(() => toolRepeatGroup(s, [0, 2], 1), /zusammenhängend/);
    assert.throws(() => toolRepeatGroup(segs(1000), [], 2), /2048/);
});

test('tool: distribute length', () => {
    assert.deepEqual(toolDistributeLength(segs(3), [], { mode: 'each', value: 7 }).map((x) => x.length_cm), [7, 7, 7]);
    const t = toolDistributeLength(segs(3), [], { mode: 'total', value: 1000 });
    assert.deepEqual(t.map((x) => x.length_cm), [334, 333, 333]);
    assert.equal(totalLengthCm(t), 1000);
    assert.throws(() => toolDistributeLength(segs(3), [], { mode: 'total', value: 2 }), /1 … 65535/);
});

test('undo and redo', () => {
    const u = new UndoStack();
    let state = { v: 1 };
    u.push(state); state = { v: 2 };
    u.push(state); state = { v: 3 };
    state = u.undo(state);
    assert.equal(state.v, 2);
    state = u.undo(state);
    assert.equal(state.v, 1);
    state = u.redo(state);
    assert.equal(state.v, 2);
    u.push(state); state = { v: 9 };
    assert.equal(u.canRedo(), false, 'a new change clears redo');
    const big = new UndoStack(100);
    for (let i = 0; i < 150; i++) big.push({ i });
    assert.equal(big.past.length, 100);
});

test('JSON round trip and import errors', () => {
    const p = { ...newPattern('Streifen'), segments: [seg(4, '#FF0000', 100), seg(2, '#000000', 0)] };
    const back = parsePreset(serializePreset(p));
    assert.equal(back.segments[0].color, '#ff0000', 'colour normalised');
    assert.deepEqual(parsePreset(serializePreset(newOverlay('Weich'))).name, 'Weich');
    assert.throws(() => parsePreset('{'), /JSON/);
    assert.throws(() => parsePreset('{"format_version":2}'), /Formatversion/);
    assert.throws(() => parsePreset(JSON.stringify({ format_version: 1, type: 'pattern', name: 'x', segments: [] })),
        /Abschnitt/);
});

test('copy name: "<name> (2)", next free number, 16 bytes', () => {
    assert.equal(copyName('Spotlight', ['Spotlight']), 'Spotlight (2)');
    assert.equal(copyName('Spotlight', ['Spotlight', 'Spotlight (2)']), 'Spotlight (3)');
    assert.equal(copyName('Spotlight (2)', ['Spotlight (2)']), 'Spotlight (3)', 'suffix replaced');
    assert.equal(copyName('Weiche Ränder', []), 'Weiche Ränd (2)');
    assert.equal(copyName('ääääääääääää', []), 'ääääää (2)', 'no cut inside ä');
    assert.equal(copyName('Ohne', []), 'Ohne (2)');
    assert.equal(copyName('Test (x)', []), 'Test (x) (2)', 'not a number suffix');
    assert.equal(copyName('1234567890123456', []), '123456789012 (2)');
    /* same cases in common/test/test_preset.c (C version for core and dash) */
});

test('bundle and file names', () => {
    const b = makeBundle([newPattern('A'), newOverlay('B')]);
    const back = parsePresetFile(serializePreset(b));
    assert.deepEqual(back.map((p) => p.type), ['pattern', 'overlay']);
    assert.equal(parsePresetFile(serializePreset(newOverlay('C'))).length, 1);
    assert.throws(() => parsePresetFile(JSON.stringify(makeBundle([{ type: 'pattern' }]))), /Preset 1/);
    assert.equal(presetFileName('Weiche Ränder'), 'weiche-raender.json');
    assert.equal(presetFileName('10-m-Muster!'), '10-m-muster.json');
    assert.equal(presetFileName('***'), 'preset.json');
});

test('tool: gradient with 3 and 4 colours, evenly spaced', () => {
    const three = toolGradient(segs(5), [], { color: { stops: ['#ff0000', '#00ff00', '#0000ff'] } });
    assert.deepEqual(three.map((x) => x.color).filter((_, i) => i % 2 === 0), ['#ff0000', '#00ff00', '#0000ff']);
    const four = toolGradient(segs(7), [], { color: { stops: ['#ff0000', '#00ff00', '#0000ff', '#ffffff'] } });
    assert.deepEqual([0, 2, 4, 6].map((i) => four[i].color), ['#ff0000', '#00ff00', '#0000ff', '#ffffff']);
    assert.throws(() => toolGradient(segs(3), [], { color: { stops: ['#ff0000'] } }), /2 … 4/);
    assert.throws(() => toolGradient(segs(3), [], { color: { stops: ['#ff0000', '#00ff00', '#0000ff', '#ffffff', '#000000'] } }), /2 … 4/);
});

test('tool: cyclic gradient repeats without a seam', () => {
    /* 3 colours over 6 segments: stops on segments 1, 3, 5; segment 6 is half way back to red */
    const out = toolGradient(segs(6), [], { color: { stops: ['#ff0000', '#00ff00', '#0000ff'], cyclic: true } });
    assert.deepEqual([0, 2, 4].map((i) => out[i].color), ['#ff0000', '#00ff00', '#0000ff']);
    assert.equal(out[5].color, mixColor('#0000ff', '#ff0000', 0.5));
    assert.equal(out[1].color, mixColor('#ff0000', '#00ff00', 0.5));
    /* same step between every neighbour, including last → first of the next repetition */
    const two = toolGradient(segs(4), [], { color: { stops: ['#000000', '#ffffff'], cyclic: true } });
    assert.deepEqual(two.map((x) => x.color), ['#000000', '#bcbcbc', '#ffffff', '#bcbcbc']);
});
