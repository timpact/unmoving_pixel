/*
 * Simulated core for the dash simulator.
 *
 * Plays back a test ride (speed profile, distances) and keeps the LED settings
 * and preset lists that the real core would report over UART (ADR 0004).
 */
#ifndef SIM_CORE_H
#define SIM_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

typedef struct {
    bool link_drops;   /* simulate a lost link for 2 s every 30 s */
} sim_options_t;

void sim_core_init(const sim_options_t *opt);

/* Advance the simulation by dt_ms and fill the model. */
void sim_core_step(uint32_t dt_ms);
void sim_core_fill_model(ui_model_t *model);

/* GUI actions (ui_actions_t callbacks) */
extern const ui_actions_t sim_core_actions;

/* Force the editor on (for screenshots) */
void sim_core_set_editor(bool on);

#endif /* SIM_CORE_H */
