/* Automated GUI checks (ADR 0016, docs/ui-guidelines.md). */
#ifndef SIM_CHECK_H
#define SIM_CHECK_H

#include <stdbool.h>

/* Runs all scenarios and checks headless. Returns the process exit code
 * (0 = all checks passed). lv_init() must have been called. */
int check_run(bool update_references);

#endif /* SIM_CHECK_H */
