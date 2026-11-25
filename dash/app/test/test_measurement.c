/*
 * Unit test for the distance measurement logic (ADR 0008, 0015).
 *
 *   cc -Wall -Wextra -I dash/app dash/app/test/test_measurement.c dash/app/measurement.c -o /tmp/test_measurement
 *   /tmp/test_measurement
 */
#include <assert.h>
#include <stdio.h>
#include "measurement.h"
int main(void) {
    measurement_t m = {0};
    bool r;
    /* net mode, start near wrap-around, go forward 1000 mm across 2^32 */
    uint32_t pos = 0xFFFFFF00u;
    measurement_start(&m, pos);
    pos += 1000;                                   /* wraps */
    assert(measurement_value(&m, pos, &r) == 1000 && !r);
    pos -= 1500;                                   /* roll back 1.5 m → -500 */
    assert(measurement_value(&m, pos, NULL) == -500);
    measurement_stop(&m, pos);
    pos += 9999;                                   /* stopped: no change */
    assert(measurement_value(&m, pos, NULL) == -500);
    /* switch counter while running: value must not jump */
    measurement_start(&m, pos);
    pos += 200;                                    /* -300 */
    uint32_t total = 123456789u;
    measurement_switch_counter(&m, pos, total);
    assert(measurement_value(&m, total, NULL) == -300);
    total += 700;                                  /* travelled adds → +400 */
    assert(measurement_value(&m, total, NULL) == 400);
    /* reset while running keeps counting from 0 */
    measurement_reset(&m, total);
    total += 50;
    assert(measurement_value(&m, total, NULL) == 50 && m.running);
    /* implausible jump (counter went backwards a lot) → reset */
    total -= 2100000000u;
    assert(measurement_value(&m, total, &r) == 0 && r && !m.running);
    puts("measurement: all tests passed");
    return 0;
}
