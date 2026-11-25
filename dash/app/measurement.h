/*
 * Distance measurement on dash (ADR 0008): stopwatch with start, stop, reset.
 *
 * Platform-independent. The state is small and is persisted by the caller
 * (dash NVS on the device) after every button press.
 */
#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stdbool.h>
#include <stdint.h>

/* Measurements above this value are treated as implausible (core replaced,
 * core NVS erased → total_mm jumped backwards, which shows up as a huge
 * unsigned difference). 2,000 km = 2^31 mm rounded down, half the uint32 range. */
#define MEASUREMENT_MAX_PLAUSIBLE_MM 2000000000u /* 2,000 km */

typedef struct {
    bool     running;
    uint32_t accumulated_mm;  /* distance of all finished intervals */
    uint32_t start_mm;        /* total_mm at the last start (valid while running) */
} measurement_t;

void measurement_start(measurement_t *m, uint32_t total_mm);
void measurement_stop(measurement_t *m, uint32_t total_mm);
void measurement_reset(measurement_t *m, uint32_t total_mm);

/* Current value in mm. Resets the measurement if the value is implausible
 * and reports that through *was_reset (may be NULL). */
uint32_t measurement_value(measurement_t *m, uint32_t total_mm, bool *was_reset);

#endif /* MEASUREMENT_H */
