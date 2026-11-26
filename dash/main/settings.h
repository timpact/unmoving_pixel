/*
 * dash NVS storage for dash/app/dash_state (ADR 0019 "NVS on the dash"):
 * namespace "dash", one uint8 key per setting.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include "dash_state.h"

/* Initialise the NVS (erased once if it is full or from an older format).
 * Returns the store, or NULL if NVS is not usable (then defaults, nothing saved). */
const dash_store_t *settings_store(void);

#endif /* SETTINGS_H */
