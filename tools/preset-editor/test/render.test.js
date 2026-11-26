/* Tests for render.js – reference values shared with the core implementation later */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { STRIP, averageOver, linearSegments, overlayFactor, renderStrip } from '../render.js';

const pattern = (segments) => ({ format_version: 1, type: 'pattern', name: 't', segments });
const white = (cm) => ({ length_cm: cm, color: '#ffffff', brightness: 100 });
const black = (cm) => ({ length_cm: cm, color: '#000000', brightness: 0 });

test('strip geometry: 72 LEDs at 144/m = 500 mm', () => {
    assert.equal(STRIP.leds, 72);
    assert.ok(Math.abs(STRIP.leds * STRIP.pitchMm - 500) < 1e-9);
});

test('overlay factor matches up_overlay.h', () => {
    const o = { level_start: 10, level_mid: 100, level_end: 20, fade_start_mm: 100, fade_end_mm: 200 };
    assert.equal(overlayFactor(o, 0), 0.1);
    assert.equal(overlayFactor(o, 50), 0.1 + 0.9 * 0.5);   /* smoothstep(0.5) = 0.5 */
    assert.equal(overlayFactor(o, 250), 1);
    assert.equal(overlayFactor(o, 500), 0.2);
    assert.equal(overlayFactor(null, 0), 1);
    /* fades longer than the strip are scaled down */
    const big = { level_start: 0, level_mid: 100, level_end: 0, fade_start_mm: 500, fade_end_mm: 500 };
    assert.equal(overlayFactor(big, 250), 1);
});

test('box filter: LED on an edge shows the average', () => {
    const segs = linearSegments(pattern([white(1), black(1)]));   /* 10 mm white, 10 mm black */
    assert.deepEqual(averageOver(segs, 0, 10), [1, 1, 1]);
    assert.deepEqual(averageOver(segs, 5, 15), [0.5, 0.5, 0.5]);
    /* wraps around the end of the pattern */
    assert.deepEqual(averageOver(segs, 15, 25), [0.5, 0.5, 0.5]);
    assert.deepEqual(averageOver(segs, -5, 5), [0.5, 0.5, 0.5]);
});

test('render: uniform pattern, brightness and global', () => {
    const p = pattern([white(10)]);
    const full = renderStrip(p, null, 0);
    assert.equal(full.length, 72);
    assert.ok(full.every((c) => c[0] === 255 && c[1] === 255 && c[2] === 255));
    const half = renderStrip(p, null, 0, { global: 0.5 });
    assert.deepEqual(half[0], [188, 188, 188], '50 % linear = sRGB 188');
    const dim = renderStrip(pattern([{ length_cm: 10, color: '#ffffff', brightness: 50 }]), null, 0);
    assert.deepEqual(dim[0], [128, 128, 128], 'brightness 50 % is perceptual');
});

test('render: ground-fixed – moving by the pattern length gives the same frame', () => {
    const p = pattern([white(3), black(2), { length_cm: 4, color: '#ff8800', brightness: 70 }]);
    const a = renderStrip(p, null, 123.4);
    const b = renderStrip(p, null, 123.4 + 90);   /* pattern length 9 cm */
    assert.deepEqual(a, b);
});

test('render: overlay darkens the strip ends', () => {
    const o = { level_start: 0, level_mid: 100, level_end: 0, fade_start_mm: 150, fade_end_mm: 150 };
    const out = renderStrip(pattern([white(10)]), o, 0);
    assert.ok(out[0][0] < 30, 'first LED nearly dark');
    assert.equal(out[36][0], 255, 'middle full');
    assert.ok(out[71][0] < 30, 'last LED nearly dark');
});
