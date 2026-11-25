/*
 * Distance measurement on dash (ADR 0008, 0015): stopwatch with start, stop, reset.
 *
 * Platform-independent. Works on one of two counters sent by core:
 *   - position_mm: net position (backward subtracts) – default
 *   - total_mm:    travelled distance (backward adds)
 * Both are uint32 and wrap; differences are interpreted as int32.
 * The caller passes the value of the selected counter. The state is small and
 * is persisted by the caller (dash NVS) after every button press.
 */
#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

/* |value| above this is treated as implausible (core replaced, core NVS erased,
 * counter jumped). 2,000 km, below the int32 range of 2,147 km. */
#define MEASUREMENT_MAX_PLAUSIBLE_MM 2000000000

typedef struct {
    bool     running;
    int32_t  accumulated_mm;  /* sum of all finished intervals (may be negative in net mode) */
    uint32_t start_mm;        /* counter value at the last start (valid while running) */
} measurement_t;

void measurement_start(measurement_t *m, uint32_t counter_mm);
void measurement_stop(measurement_t *m, uint32_t counter_mm);
void measurement_reset(measurement_t *m, uint32_t counter_mm);

/* The selected counter changes (setting "subtract/add backward travel"):
 * ends the running interval on the old counter and continues on the new one,
 * so that the value does not jump. */
void measurement_switch_counter(measurement_t *m, uint32_t old_counter_mm, uint32_t new_counter_mm);

/* Current value in mm. Resets the measurement if the value is implausible
 * and reports that through *was_reset (may be NULL). */
int32_t measurement_value(measurement_t *m, uint32_t counter_mm, bool *was_reset);

#endif /* MEASUREMENT_H */
