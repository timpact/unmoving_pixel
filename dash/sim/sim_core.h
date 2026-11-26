/*
 * Simulated core for the dash simulator.
 *
 * Plays back a test ride (speed profile, distances) and keeps the LED settings
 * and preset lists that the real core would report over UART (ADR 0004).
 * The dash-owned state (settings, measurements, ride panel) is in
 * dash/app/dash_state, not here.
 */
#ifndef SIM_CORE_H
#define SIM_CORE_H

#include <stdbool.h>
#include <stdint.h>

#include "ui.h"

typedef struct {
    bool link_drops;      /* simulate a lost link for 2 s every 30 s */
    bool overlays_full;   /* all user overlay slots taken ("no free slot" notice) */
} sim_options_t;

void sim_core_init(const sim_options_t *opt);

/* Advance the simulation by dt_ms and fill the model. */
void sim_core_step(uint32_t dt_ms);
void sim_core_fill_model(ui_model_t *model);

/* Counters of the status frame (ADR 0004, 0015) */
void sim_core_counters(uint32_t *total_mm, uint32_t *position_mm);

/* GUI actions of core (ui_actions_t callbacks); combine with dash_state_actions() */
extern const ui_actions_t sim_core_actions;

/* Force the editor on (for screenshots) */
void sim_core_set_editor(bool on);

#endif /* SIM_CORE_H */
