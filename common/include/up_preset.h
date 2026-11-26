/*
 * Preset helpers shared by core and dash (ADR 0011, 0012, 0017).
 *
 * up_preset_copy_name(): name of an automatic copy, "<name> (2)" or the next
 * free number, at most 16 bytes UTF-8. An existing " (n)" suffix is replaced,
 * not repeated. Same behaviour as copyName() in tools/preset-editor/model.js;
 * both are tested with the same cases (common/test/test_preset.c,
 * tools/preset-editor/test/model.test.js).
 */
#ifndef UP_PRESET_H
#define UP_PRESET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define UP_PRESET_NAME_BYTES 16

static inline bool up_preset_name_taken(const char *name, const char *const *taken, size_t n_taken)
{
    for (size_t i = 0; i < n_taken; i++) {
        if (taken[i] != NULL && strcmp(taken[i], name) == 0) {
            return true;
        }
    }
    return false;
}

/* out must hold UP_PRESET_NAME_BYTES + 1 bytes */
static inline void up_preset_copy_name(const char *name, const char *const *taken, size_t n_taken,
                                       char *out)
{
    char base[UP_PRESET_NAME_BYTES + 1];
    size_t len = strlen(name);
    if (len > UP_PRESET_NAME_BYTES) {
        len = UP_PRESET_NAME_BYTES;
    }
    memcpy(base, name, len);
    base[len] = '\0';

    /* strip an existing " (n)" suffix */
    if (len >= 4 && base[len - 1] == ')') {
        size_t i = len - 2;
        while (i > 0 && base[i] >= '0' && base[i] <= '9') {
            i--;
        }
        if (i < len - 2 && base[i] == '(' && i >= 1 && base[i - 1] == ' ') {
            len = i - 1;
            base[len] = '\0';
        }
    }

    for (unsigned k = 2; k < 1000; k++) {
        char suffix[8];
        int sl = snprintf(suffix, sizeof(suffix), " (%u)", k);
        size_t max = UP_PRESET_NAME_BYTES - (size_t)sl;
        size_t hl = len < max ? len : max;
        /* do not cut a UTF-8 character: back off over continuation bytes */
        while (hl > 0 && hl < len && ((unsigned char)base[hl] & 0xC0) == 0x80) {
            hl--;
        }
        while (hl > 0 && base[hl - 1] == ' ') {
            hl--;
        }
        memcpy(out, base, hl);
        memcpy(out + hl, suffix, (size_t)sl + 1);
        if (!up_preset_name_taken(out, taken, n_taken)) {
            return;
        }
    }
}

#endif /* UP_PRESET_H */
