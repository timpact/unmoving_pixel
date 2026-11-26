/*
 * Mock of dash + core for local development (ADR 0017).
 *
 * Behaves like the real devices behind the HTTP API of ADR 0013:
 *   - built-in presets are read-only (from presets/ in the repository)
 *   - user slots: 32 patterns, 8 overlays (ADR 0011, 0012)
 *   - every write increments the revision of the slot; PUT carries the base
 *     revision and is answered with 409 if it no longer matches
 *   - overlay changes "on the dash" write into the active preset; a built-in
 *     preset is copied to a free user slot first ("<name> (2)")
 *
 * Slot numbers (mock only, the real numbering comes from core):
 *   patterns: 0 … 15 built-in, 16 … 47 user
 *   overlays: 0 … 7 built-in,  8 … 15 user
 *
 * Methods return { status, body } like an HTTP answer.
 */
import { copyName, makeBundle, totalLengthCm, validateOverlay, validatePattern } from './model.js';

export const SLOTS = Object.freeze({
    patterns: Object.freeze({ builtin: 16, user: 32 }),
    overlays: Object.freeze({ builtin: 8, user: 8 }),
});

const OVERLAY_KEYS = ['level_start', 'level_mid', 'level_end', 'fade_start_mm', 'fade_end_mm'];

const ok = (body) => ({ status: 200, body });
const fail = (status, error, extra = {}) => ({ status, body: { error, ...extra } });

function overlayValues(preset) {
    return Object.fromEntries(OVERLAY_KEYS.map((k) => [k, preset[k]]));
}

export class MockDash {
    constructor(builtins = { patterns: [], overlays: [] }) {
        this.slots = { patterns: new Map(), overlays: new Map() };
        this.lastRevision = { patterns: new Map(), overlays: new Map() };
        for (const cat of ['patterns', 'overlays']) {
            builtins[cat].slice(0, SLOTS[cat].builtin).forEach((preset, slot) => {
                this.slots[cat].set(slot, { preset: structuredClone(preset), revision: 1, builtin: true });
            });
        }
        this.listRev = 1;
        this.preview = { patterns: null, overlays: null };
        this.timeMs = null;
        const firstOverlay = this.slots.overlays.get(0)?.preset;
        this.settings = {
            global_brightness: 80,
            active_pattern: 0,
            active_overlay: 0,
            overlay: firstOverlay ? overlayValues(firstOverlay) : null,
            /* strip layout (ADR 0018) */
            main_leds: 72,
            front_leds: 4,
            rear_leds: 4,
            layout_flags: 3,
        };
    }

    validCategory(cat) {
        return cat === 'patterns' || cat === 'overlays';
    }

    isUserSlot(cat, slot) {
        const s = SLOTS[cat];
        return Number.isInteger(slot) && slot >= s.builtin && slot < s.builtin + s.user;
    }

    validate(cat, preset) {
        if (cat === 'patterns') return preset?.type === 'pattern' ? validatePattern(preset) : [{ path: 'type', message: 'Kein Muster' }];
        return preset?.type === 'overlay' ? validateOverlay(preset) : [{ path: 'type', message: 'Kein Overlay' }];
    }

    /* All built-in slots that exist plus every user slot (empty ones included) */
    list(cat) {
        const out = [];
        const total = SLOTS[cat].builtin + SLOTS[cat].user;
        for (let slot = 0; slot < total; slot++) {
            const e = this.slots[cat].get(slot);
            if (!e && !this.isUserSlot(cat, slot)) continue;
            out.push({
                slot,
                kind: e ? (e.builtin ? 'builtin' : 'user') : 'empty',
                revision: e ? e.revision : null,
                name: e ? e.preset.name : '',
                length_cm: e && cat === 'patterns' ? totalLengthCm(e.preset.segments) : 0,
            });
        }
        return out;
    }

    state() {
        return ok({
            settings: structuredClone(this.settings),
            preset_list_rev: this.listRev,
            time_valid: this.timeMs !== null,
            patterns: this.list('patterns'),
            overlays: this.list('overlays'),
        });
    }

    get(cat, slot) {
        const e = this.slots[cat]?.get(slot);
        if (!e) return fail(404, 'Preset nicht gefunden');
        return ok({ slot, builtin: e.builtin, revision: e.revision, preset: structuredClone(e.preset) });
    }

    store(cat, slot, preset) {
        const rev = (this.lastRevision[cat].get(slot) ?? 0) + 1;
        this.lastRevision[cat].set(slot, rev);
        this.slots[cat].set(slot, { preset: structuredClone(preset), revision: rev, builtin: false });
        this.listRev += 1;
        if (cat === 'overlays' && this.settings.active_overlay === slot) {
            this.settings.overlay = overlayValues(preset);
        }
        return rev;
    }

    /* PUT /api/{cat}/{slot} with { base_revision, preset }; base_revision null = slot expected empty */
    put(cat, slot, body) {
        if (!this.validCategory(cat)) return fail(404, 'Unbekannte Kategorie');
        if (this.slots[cat].get(slot)?.builtin) return fail(403, 'Eingebaute Presets können nicht überschrieben werden');
        if (!this.isUserSlot(cat, slot)) return fail(404, 'Slot existiert nicht');
        const errors = this.validate(cat, body?.preset);
        if (errors.length > 0) return fail(400, errors.map((e) => e.message).join('; '), { errors });
        const current = this.slots[cat].get(slot);
        const currentRev = current ? current.revision : null;
        if ((body.base_revision ?? null) !== currentRev) {
            return fail(409, 'Preset wurde inzwischen geändert', {
                revision: currentRev, preset: current ? structuredClone(current.preset) : null,
            });
        }
        const revision = this.store(cat, slot, body.preset);
        return ok({ slot, revision });
    }

    delete(cat, slot) {
        if (!this.validCategory(cat)) return fail(404, 'Unbekannte Kategorie');
        const e = this.slots[cat].get(slot);
        if (!e) return fail(404, 'Preset nicht gefunden');
        if (e.builtin) return fail(403, 'Eingebaute Presets können nicht gelöscht werden');
        this.slots[cat].delete(slot);
        this.listRev += 1;
        const key = cat === 'patterns' ? 'active_pattern' : 'active_overlay';
        if (this.settings[key] === slot) this.select(cat, 0);
        return ok({ slot });
    }

    /* POST /api/preview/{cat}: try on the strip without storing */
    setPreview(cat, preset) {
        if (!this.validCategory(cat)) return fail(404, 'Unbekannte Kategorie');
        const errors = this.validate(cat, preset);
        if (errors.length > 0) return fail(400, errors.map((e) => e.message).join('; '), { errors });
        this.preview[cat] = structuredClone(preset);
        if (cat === 'overlays') this.settings.overlay = overlayValues(preset);
        return ok({});
    }

    exportAll() {
        const user = (cat) => [...this.slots[cat].entries()]
            .filter(([, e]) => !e.builtin).sort(([a], [b]) => a - b).map(([, e]) => structuredClone(e.preset));
        return ok(makeBundle([...user('patterns'), ...user('overlays')]));
    }

    setTime(body) {
        if (!Number.isFinite(body?.epoch_ms)) return fail(400, 'Zeit fehlt');
        this.timeMs = body.epoch_ms;
        return ok({});
    }

    /* ---- actions on the dash (touch screen), used by the mock control page ---- */

    select(cat, slot) {
        const e = this.slots[cat]?.get(slot);
        if (!e) return fail(404, 'Preset nicht gefunden');
        if (cat === 'patterns') {
            this.settings.active_pattern = slot;
        } else {
            this.settings.active_overlay = slot;
            this.settings.overlay = overlayValues(e.preset);
        }
        this.preview[cat] = null;
        return ok({ slot });
    }

    /* Overlay slider on the dash (ADR 0012, 0017): writes into the active preset */
    dashSetOverlay(values) {
        let slot = this.settings.active_overlay;
        let e = this.slots.overlays.get(slot);
        if (!e) return fail(404, 'Kein aktives Overlay');
        const next = { ...e.preset, ...values };
        const errors = validateOverlay(next);
        if (errors.length > 0) return fail(400, errors.map((x) => x.message).join('; '));
        if (e.builtin) {
            const free = this.list('overlays').find((x) => x.kind === 'empty');
            if (!free) return fail(507, 'Kein freier Platz – im Editor ein Overlay löschen');
            const names = this.list('overlays').map((x) => x.name);
            next.name = copyName(e.preset.name, names);
            slot = free.slot;
            this.settings.active_overlay = slot;
        }
        this.store('overlays', slot, next);
        this.settings.overlay = overlayValues(next);
        return ok({ slot });
    }

    /* Strip layout set on the dash (ADR 0018) */
    setLayout(values) {
        const { main_leds: m = this.settings.main_leds, front_leds: f = this.settings.front_leds,
            rear_leds: r = this.settings.rear_leds } = values ?? {};
        const ok8 = (v, min, max) => Number.isInteger(v) && v >= min && v <= max;
        if (!ok8(m, 36, 144) || !ok8(f, 0, 8) || !ok8(r, 0, 8)) return fail(400, 'Hauptteil 36 … 144, Enden 0 … 8 LEDs');
        Object.assign(this.settings, { main_leds: m, front_leds: f, rear_leds: r });
        return ok({});
    }

    /* Simulates a change of a user preset from elsewhere (for conflict tests) */
    touch(cat, slot) {
        const e = this.slots[cat]?.get(slot);
        if (!e || e.builtin) return fail(404, 'Kein eigenes Preset in diesem Slot');
        const p = structuredClone(e.preset);
        if (cat === 'patterns') {
            p.segments[0].brightness = p.segments[0].brightness >= 50 ? p.segments[0].brightness - 10 : p.segments[0].brightness + 10;
        } else {
            p.level_mid = p.level_mid >= 50 ? p.level_mid - 10 : p.level_mid + 10;
        }
        this.store(cat, slot, p);
        return ok({ slot, revision: this.slots[cat].get(slot).revision });
    }
}
