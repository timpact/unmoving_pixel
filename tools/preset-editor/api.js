/*
 * Access to the presets (ADR 0013, 0017).
 *
 *   DashApi   – HTTP API of the dash (or of the dev server's mock dash)
 *   LocalApi  – local mode without scooter: same slot logic in the browser,
 *               user presets kept in localStorage, built-ins from
 *               presets/index.json if the page is served by the dev server
 *
 * Both have the same methods; errors are thrown as ApiError with the HTTP
 * status (0 = no connection) and the German message from the dash.
 */
import { MockDash } from './mock-dash.mjs';

export class ApiError extends Error {
    constructor(status, message, body = null) {
        super(message);
        this.status = status;
        this.body = body;
    }
}

const TIMEOUT_MS = 3000;

export class DashApi {
    constructor(base = '') {
        this.base = base;
        this.mode = 'dash';
    }

    async request(method, path, body) {
        const ctrl = new AbortController();
        const timer = setTimeout(() => ctrl.abort(), TIMEOUT_MS);
        let res;
        try {
            res = await fetch(this.base + path, {
                method,
                headers: body === undefined ? {} : { 'Content-Type': 'application/json' },
                body: body === undefined ? undefined : JSON.stringify(body),
                signal: ctrl.signal,
                cache: 'no-store',
            });
        } catch {
            throw new ApiError(0, 'Keine Verbindung zum dash');
        } finally {
            clearTimeout(timer);
        }
        let data = null;
        try {
            data = await res.json();
        } catch {
            /* empty or non-JSON answer */
        }
        if (!res.ok) throw new ApiError(res.status, data?.error ?? `Fehler ${res.status}`, data);
        return data;
    }

    state() { return this.request('GET', '/api/state'); }
    get(cat, slot) { return this.request('GET', `/api/${cat}/${slot}`); }
    put(cat, slot, baseRevision, preset) {
        return this.request('PUT', `/api/${cat}/${slot}`, { base_revision: baseRevision, preset });
    }
    delete(cat, slot) { return this.request('DELETE', `/api/${cat}/${slot}`); }
    preview(cat, preset) { return this.request('POST', `/api/preview/${cat}`, preset); }
    exportAll() { return this.request('GET', '/api/export'); }
    setTime() {
        return this.request('POST', '/api/time', {
            epoch_ms: Date.now(), tz_offset_min: -new Date().getTimezoneOffset(),
        });
    }
}

const STORAGE_KEY = 'unmoving-pixel.editor.local.v1';

export class LocalApi {
    constructor(builtins) {
        this.mode = 'local';
        this.dash = new MockDash(builtins);
        this.restore();
    }

    restore() {
        let saved = null;
        try {
            saved = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? 'null');
        } catch {
            saved = null;
        }
        if (!saved) return;
        for (const cat of ['patterns', 'overlays']) {
            for (const { slot, revision, preset } of saved[cat] ?? []) {
                if (!this.dash.isUserSlot(cat, slot) || this.dash.validate(cat, preset).length > 0) continue;
                this.dash.slots[cat].set(slot, { preset, revision, builtin: false });
                this.dash.lastRevision[cat].set(slot, revision);
            }
        }
    }

    persist() {
        const out = {};
        for (const cat of ['patterns', 'overlays']) {
            out[cat] = [...this.dash.slots[cat].entries()]
                .filter(([, e]) => !e.builtin)
                .map(([slot, e]) => ({ slot, revision: e.revision, preset: e.preset }));
        }
        try {
            localStorage.setItem(STORAGE_KEY, JSON.stringify(out));
        } catch {
            /* storage blocked: presets live until the page is closed */
        }
    }

    async call(r, persist = false) {
        if (r.status !== 200) throw new ApiError(r.status, r.body?.error ?? `Fehler ${r.status}`, r.body);
        if (persist) this.persist();
        return structuredClone(r.body);
    }

    state() { return this.call(this.dash.state()); }
    get(cat, slot) { return this.call(this.dash.get(cat, slot)); }
    put(cat, slot, baseRevision, preset) {
        return this.call(this.dash.put(cat, slot, { base_revision: baseRevision, preset }), true);
    }
    delete(cat, slot) { return this.call(this.dash.delete(cat, slot), true); }
    async preview() { throw new ApiError(0, 'Im lokalen Modus ist kein Strip verbunden'); }
    exportAll() { return this.call(this.dash.exportAll()); }
    async setTime() { return {}; }
}

/* Uses the dash if its API answers, otherwise the local mode */
export async function connect() {
    const dash = new DashApi();
    try {
        await dash.state();
        return dash;
    } catch {
        /* no dash API */
    }
    let builtins = { patterns: [], overlays: [] };
    try {
        const res = await fetch('presets/index.json', { cache: 'no-store' });
        if (res.ok) builtins = await res.json();
    } catch {
        /* not served by the dev server */
    }
    return new LocalApi(builtins);
}
