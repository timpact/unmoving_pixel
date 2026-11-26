/*
 * Unit test for common/include/up_preset.h – same cases as copyName() in
 * tools/preset-editor/test/model.test.js.
 *
 *   cc -Wall -Wextra -I common/include common/test/test_preset.c -o /tmp/test_preset && /tmp/test_preset
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "up_preset.h"

static void expect(const char *name, const char *const *taken, size_t n, const char *want)
{
    char out[UP_PRESET_NAME_BYTES + 1];
    up_preset_copy_name(name, taken, n, out);
    if (strcmp(out, want) != 0) {
        printf("FAIL: copy of \"%s\" → \"%s\", expected \"%s\"\n", name, out, want);
        assert(0);
    }
    assert(strlen(out) <= UP_PRESET_NAME_BYTES);
}

int main(void)
{
    const char *t1[] = {"Spotlight"};
    const char *t2[] = {"Spotlight", "Spotlight (2)"};
    const char *t3[] = {"Spotlight (2)"};

    expect("Spotlight", t1, 1, "Spotlight (2)");
    expect("Spotlight", t2, 2, "Spotlight (3)");
    expect("Spotlight (2)", t3, 1, "Spotlight (3)");            /* suffix replaced */
    expect("Weiche Ränder", NULL, 0, "Weiche Ränd (2)");        /* 16 bytes, ä = 2 bytes */
    expect("ääääääääääää", NULL, 0, "ääääää (2)");             /* no cut inside ä */
    expect("Ohne", NULL, 0, "Ohne (2)");
    expect("Test (x)", NULL, 0, "Test (x) (2)");                /* not a number suffix */
    expect("1234567890123456", NULL, 0, "123456789012 (2)");

    printf("test_preset: all passed\n");
    return 0;
}
