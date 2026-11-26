/* Tests for the mock dash – it must behave like dash + core (ADR 0013, 0017) */
import assert from 'node:assert/strict';
import { test } from 'node:test';

import { MockDash, SLOTS } from '../mock-dash.mjs';
import { newOverlay, newPattern } from '../model.js';

const builtins = () => ({
    patterns: [newPattern('Streifen')],
    overlays: [newOverlay('Ohne'), newOverlay('Spotlight')],
});

test('list: built-ins first, then all user slots', () => {
    const d = new MockDash(builtins());
    const list = d.list('overlays');
    assert.equal(list.length, 2 + SLOTS.overlays.user);
    assert.deepEqual(list.slice(0, 3).map((e) => e.kind), ['builtin', 'builtin', 'empty']);
    assert.equal(list[2].slot, SLOTS.overlays.builtin);
});

test('built-in presets are read-only', () => {
    const d = new MockDash(builtins());
    assert.equal(d.put('patterns', 0, { base_revision: 1, preset: newPattern('x') }).status, 403);
    assert.equal(d.delete('patterns', 0).status, 403);
});

test('revisions and 409 conflict', () => {
    const d = new MockDash(builtins());
    const slot = SLOTS.patterns.builtin;
    const a = d.put('patterns', slot, { base_revision: null, preset: newPattern('A') });
    assert.deepEqual(a.body, { slot, revision: 1 });
    assert.equal(d.put('patterns', slot, { base_revision: null, preset: newPattern('B') }).status, 409, 'slot no longer empty');
    assert.equal(d.put('patterns', slot, { base_revision: 1, preset: newPattern('B') }).body.revision, 2);
    const conflict = d.put('patterns', slot, { base_revision: 1, preset: newPattern('C') });
    assert.equal(conflict.status, 409);
    assert.equal(conflict.body.revision, 2);
    assert.equal(conflict.body.preset.name, 'B');
});

test('invalid presets are rejected', () => {
    const d = new MockDash(builtins());
    const r = d.put('patterns', SLOTS.patterns.builtin, { base_revision: null, preset: { ...newPattern(), segments: [] } });
    assert.equal(r.status, 400);
    assert.equal(d.put('patterns', SLOTS.patterns.builtin, { base_revision: null, preset: newOverlay() }).status, 400);
});

test('revision stays monotonic after delete and re-create', () => {
    const d = new MockDash(builtins());
    const slot = SLOTS.overlays.builtin;
    d.put('overlays', slot, { base_revision: null, preset: newOverlay('A') });
    d.delete('overlays', slot);
    assert.equal(d.put('overlays', slot, { base_revision: null, preset: newOverlay('B') }).body.revision, 2);
});

test('dash slider on a built-in overlay creates "<name> (2)" and selects it', () => {
    const d = new MockDash(builtins());
    d.select('overlays', 1);
    const r = d.dashSetOverlay({ level_mid: 70 });
    assert.equal(r.status, 200);
    const copy = d.get('overlays', r.body.slot).body;
    assert.equal(copy.preset.name, 'Spotlight (2)');
    assert.equal(copy.preset.level_mid, 70);
    assert.equal(d.settings.active_overlay, r.body.slot);
    assert.equal(d.get('overlays', 1).body.preset.level_mid, 100, 'built-in unchanged');
    /* next change goes into the copy, no second copy */
    const r2 = d.dashSetOverlay({ level_mid: 60 });
    assert.equal(r2.body.slot, r.body.slot);
    assert.equal(d.get('overlays', r.body.slot).body.revision, 2);
});

test('dash slider without free slot keeps the built-in values', () => {
    const d = new MockDash(builtins());
    for (let i = 0; i < SLOTS.overlays.user; i++) {
        d.put('overlays', SLOTS.overlays.builtin + i, { base_revision: null, preset: newOverlay(`U${i}`) });
    }
    const r = d.dashSetOverlay({ level_mid: 50 });
    assert.equal(r.status, 507);
    assert.match(r.body.error, /Kein freier Platz/);
    assert.equal(d.settings.overlay.level_mid, 100);
});

test('deleting the active preset falls back to slot 0', () => {
    const d = new MockDash(builtins());
    const slot = SLOTS.patterns.builtin;
    d.put('patterns', slot, { base_revision: null, preset: newPattern('A') });
    d.select('patterns', slot);
    d.delete('patterns', slot);
    assert.equal(d.settings.active_pattern, 0);
});

test('export contains only user presets', () => {
    const d = new MockDash(builtins());
    d.put('overlays', SLOTS.overlays.builtin, { base_revision: null, preset: newOverlay('Mein') });
    assert.deepEqual(d.exportAll().body.presets.map((p) => p.name), ['Mein']);
});
