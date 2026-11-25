#include "measurement.h"

#include <stddef.h>

/* Difference of two wrapping uint32 counters, as signed value */
static int32_t diff(uint32_t now, uint32_t start)
{
    return (int32_t)(now - start);
}

void measurement_start(measurement_t *m, uint32_t counter_mm)
{
    if (!m->running) {
        m->running = true;
        m->start_mm = counter_mm;
    }
}

void measurement_stop(measurement_t *m, uint32_t counter_mm)
{
    if (m->running) {
        m->accumulated_mm += diff(counter_mm, m->start_mm);
        m->running = false;
    }
}

void measurement_reset(measurement_t *m, uint32_t counter_mm)
{
    m->accumulated_mm = 0;
    m->start_mm = counter_mm;   /* keeps counting from 0 if running */
}

void measurement_switch_counter(measurement_t *m, uint32_t old_counter_mm, uint32_t new_counter_mm)
{
    if (m->running) {
        m->accumulated_mm += diff(old_counter_mm, m->start_mm);
        m->start_mm = new_counter_mm;
    }
}

int32_t measurement_value(measurement_t *m, uint32_t counter_mm, bool *was_reset)
{
    int64_t value = m->accumulated_mm;
    if (m->running) {
        value += diff(counter_mm, m->start_mm);
    }

    bool reset = value > MEASUREMENT_MAX_PLAUSIBLE_MM || value < -MEASUREMENT_MAX_PLAUSIBLE_MM;
    if (reset) {
        m->running = false;
        m->accumulated_mm = 0;
        m->start_mm = counter_mm;
        value = 0;
    }
    if (was_reset != NULL) {
        *was_reset = reset;
    }
    return (int32_t)value;
}
