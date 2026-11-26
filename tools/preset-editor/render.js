/*
 * Strip rendering for the editor preview (ADR 0011, 0012).
 *
 * Same algorithm as core, so the preview matches the real strip:
 *   1. pattern value of an LED = average of colour × brightness over the LED
 *      footprint [x, x + pitch) on the ground (box filter), in linear light;
 *      the pattern repeats endlessly
 *   2. × overlay factor at the LED's position on the strip (edge fade, S-curves)
 *   3. × global brightness
 *   4. encode to sRGB
 *
 * Segment brightness (0 … 100 %) is perceptual: it is converted to linear light
 * with the sRGB transfer function, like a colour channel.
 */
import { hexToRgb, linearToSrgb, srgbToLinear } from './model.js';

/* Geometry of the main part of a strip with `leds` LEDs (ADR 0003, 0018) */
export function stripGeometry(leds) {
    const pitchMm = 1000 / 144;   /* 144 LEDs/m */
    return Object.freeze({ leds, pitchMm, lengthMm: leds * pitchMm });
}

/* Default: 72 LEDs = 0.5 m (used without dash settings, e.g. in local mode) */
export const STRIP = stripGeometry(72);

function smoothstep(t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    return t * t * (3 - 2 * t);
}

/* Overlay factor 0 … 1 at strip position s (mm), identical to common/include/up_overlay.h */
export function overlayFactor(o, sMm, lenMm = STRIP.lengthMm) {
    if (!o) return 1;
    let fs = o.fade_start_mm;
    let fe = o.fade_end_mm;
    if (fs + fe > lenMm && fs + fe > 0) {
        const k = lenMm / (fs + fe);
        fs *= k;
        fe *= k;
    }
    const start = o.level_start / 100;
    const mid = o.level_mid / 100;
    const end = o.level_end / 100;
    if (fs > 0 && sMm < fs) return start + (mid - start) * smoothstep(sMm / fs);
    if (fe > 0 && sMm > lenMm - fe) return end + (mid - end) * smoothstep((lenMm - sMm) / fe);
    return mid;
}

/* Pattern as list of { start, end, rgb } in mm and linear light (colour × brightness) */
export function linearSegments(pattern) {
    let pos = 0;
    return pattern.segments.map((s) => {
        const b = srgbToLinear(s.brightness / 100);
        const rgb = hexToRgb(s.color).map((c) => srgbToLinear(c / 255) * b);
        const seg = { start: pos, end: pos + s.length_cm * 10, rgb };
        pos = seg.end;
        return seg;
    });
}

/* Average linear colour of the repeating pattern over [x0, x1) mm (box filter) */
export function averageOver(segs, x0, x1) {
    const total = segs[segs.length - 1].end;
    const width = x1 - x0;
    const sum = [0, 0, 0];
    let x = ((x0 % total) + total) % total;   /* position within one repetition */
    let remaining = width;
    let i = segs.findIndex((s) => x < s.end);
    while (remaining > 1e-9) {
        const s = segs[i];
        const take = Math.min(s.end - x, remaining);
        for (let k = 0; k < 3; k++) sum[k] += s.rgb[k] * take;
        remaining -= take;
        x += take;
        i += 1;
        if (i >= segs.length) {   /* wrap to the start of the pattern */
            i = 0;
            x = 0;
        }
    }
    return sum.map((v) => v / width);
}

/*
 * Renders the strip at ground offset `offsetMm` (distance travelled).
 * Returns an array of [r, g, b] (0 … 255, sRGB) per LED.
 */
export function renderStrip(pattern, overlay, offsetMm, { leds = STRIP.leds, pitchMm = STRIP.pitchMm, global = 1 } = {}) {
    const segs = linearSegments(pattern);
    const lenMm = leds * pitchMm;
    const out = [];
    for (let i = 0; i < leds; i++) {
        const x = offsetMm + i * pitchMm;
        const lin = averageOver(segs, x, x + pitchMm);
        const f = overlayFactor(overlay, (i + 0.5) * pitchMm, lenMm) * global;
        out.push(lin.map((c) => Math.round(linearToSrgb(Math.min(1, c * f)) * 255)));
    }
    return out;
}
