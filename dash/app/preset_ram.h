/*
 * Preset store in RAM for the demo core (ADR 0013, 0017, 0019): behaves like
 * core behind the UART link and like the mock dash of the preset editor
 * (tools/preset-editor/mock-dash.mjs), with the same rules:
 *   - built-in presets (generated from presets/) are read-only
 *   - every write increments the revision of the slot, also after delete and
 *     re-create; PUT carries the base revision (conflict otherwise)
 *   - overlay changes on the dash write into the active preset; a built-in
 *     preset is copied to a free user slot first ("<name> (2)")
 *   - deleting the active preset selects slot 0
 *
 * Slot numbers (as the mock dash):
 *   patterns: 0 … 15 built-in, 16 … 47 user
 *   overlays: 0 … 7 built-in,  8 … 15 user
 * User presets are lost on restart (until the UART link exists).
 */
#ifndef PRESET_RAM_H
#define PRESET_RAM_H

#include "preset_store.h"

#define PRESET_RAM_PATTERN_BUILTIN 16
#define PRESET_RAM_PATTERN_USER    32
#define PRESET_RAM_OVERLAY_BUILTIN 8
#define PRESET_RAM_OVERLAY_USER    8

/* Load the built-in presets (up_presets_builtin.h); frees user presets */
void preset_ram_init(void);

extern const preset_store_t preset_ram_store;

/* Actions on the dash (touch GUI) */
ps_result_t preset_ram_select(uint8_t cat, uint8_t slot);
/* Overlay sliders: write into the active preset, copy a built-in one first.
 * PS_FULL: no free user slot, nothing changed. *slot = slot written. */
ps_result_t preset_ram_dash_set_overlay(const up_overlay_t *values, uint8_t *slot);

uint8_t preset_ram_active(uint8_t cat);
/* Overlay values in effect (active preset or the overlay being tried) */
up_overlay_t preset_ram_overlay(void);

#endif /* PRESET_RAM_H */
