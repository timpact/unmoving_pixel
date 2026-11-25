#include "measurement.h"

#include <stddef.h>

void measurement_start(measurement_t *m, uint32_t total_mm)
{
    if (!m->running) {
        m->running = true;
        m->start_mm = total_mm;
    }
}

void measurement_stop(measurement_t *m, uint32_t total_mm)
{
    if (m->running) {
        /* unsigned arithmetic: correct across a wrap-around of total_mm */
        m->accumulated_mm += total_mm - m->start_mm;
        m->running = false;
    }
}

void measurement_reset(measurement_t *m, uint32_t total_mm)
{
    m->accumulated_mm = 0;
    m->start_mm = total_mm;   /* keeps counting from 0 if running */
}

uint32_t measurement_value(measurement_t *m, uint32_t total_mm, bool *was_reset)
{
    uint32_t value = m->accumulated_mm;
    if (m->running) {
        value += total_mm - m->start_mm;
    }

    bool reset = value > MEASUREMENT_MAX_PLAUSIBLE_MM;
    if (reset) {
        m->running = false;
        m->accumulated_mm = 0;
        m->start_mm = total_mm;
        value = 0;
    }
    if (was_reset != NULL) {
        *was_reset = reset;
    }
    return value;
}
