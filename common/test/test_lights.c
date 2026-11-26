/*
 * Unit test for the end lights (ADR 0018), common/src/up_lights.c.
 *
 *   cc -Wall -Wextra -I common/include common/test/test_lights.c common/src/up_lights.c -lm -o /tmp/test_lights && /tmp/test_lights
 */
#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "up_lights.h"

#define NEAR(a, b) (fabsf((a) - (b)) < 1e-4f)

static up_lights_t L;
static up_lights_out_t O;
static uint32_t t;
static uint32_t mm;

/* advance in 1 ms steps at the given speed (mm/s), travelling accordingly */
static void run(uint32_t ms, int32_t speed)
{
    static float frac;
    for (uint32_t i = 0; i < ms; i++) {
        t++;
        frac += (float)(speed < 0 ? -speed : speed) / 1000.0f;
        mm += (uint32_t)frac;
        frac -= (uint32_t)frac;
        up_lights_update(&L, t, speed, mm);
    }
    up_lights_output(&L, &O);
}

static void test_envelope(void)
{
    assert(up_blink_envelope(0) == 0.0f);
    assert(NEAR(up_blink_envelope(50), 0.5f));            /* half way up the S-curve */
    assert(up_blink_envelope(100) == 1.0f);
    assert(up_blink_envelope(349) == 1.0f);
    assert(NEAR(up_blink_envelope(425), 0.5f));           /* half way down */
    assert(up_blink_envelope(500) == 0.0f);
    assert(up_blink_envelope(666) == 0.0f);
    assert(up_blink_envelope(667 + 50) == up_blink_envelope(50));   /* periodic */
    /* smooth: no step larger than a few percent per ms */
    for (uint32_t ms = 1; ms < 2000; ms++) {
        assert(fabsf(up_blink_envelope(ms) - up_blink_envelope(ms - 1)) < 0.03f);
    }
}

static void test_signal_and_timeout(void)
{
    t = 1000;
    mm = 0;
    up_lights_init(&L, t);
    up_lights_set_settings(&L, 100, true, 10);
    run(10, 0);
    up_lights_set_signal(&L, UP_SIGNAL_LEFT, t, mm);
    run(200, 0);                                        /* in the full phase */
    assert(O.front[UP_SIDE_LEFT][0] > 0.99f && O.rear[UP_SIDE_LEFT][1] > 0.18f);   /* amber */
    assert(O.front[UP_SIDE_RIGHT][0] == 0.0f);
    assert((up_lights_status(&L) & UP_LIGHTS_SIGNAL_MASK) == UP_SIGNAL_LEFT);
    assert(up_lights_status(&L) & UP_LIGHTS_BLINK_ON);

    /* off after 30 s standing still */
    run(30000, 0);
    assert(L.signal == UP_SIGNAL_OFF);
    run(200, 0);
    assert(O.front[UP_SIDE_LEFT][0] == 0.0f);

    /* off after 10 m riding at 2 m/s = 5 s */
    up_lights_set_signal(&L, UP_SIGNAL_RIGHT, t, mm);
    run(4900, 2000);
    assert(L.signal == UP_SIGNAL_RIGHT);
    run(200, 2000);
    assert(L.signal == UP_SIGNAL_OFF);

    /* hazard lights stay on */
    up_lights_set_signal(&L, UP_SIGNAL_HAZARD, t, mm);
    run(60000, 2000);
    assert(L.signal == UP_SIGNAL_HAZARD);
    run(1, 2000);
    assert(NEAR(O.front[0][0], O.front[1][0]));         /* both sides in sync */
    up_lights_set_signal(&L, UP_SIGNAL_OFF, t, mm);
    run(1000, 0);
}

static void test_smooth_switch_off_and_light(void)
{
    t = 5000;
    mm = 0;
    up_lights_init(&L, t);
    up_lights_set_settings(&L, 100, true, 10);
    up_lights_set_light(&L, true, t);
    run(0, 0);
    up_lights_output(&L, &O);
    assert(O.front[0][0] == 0.0f);                      /* fades in, no hard switch */
    run(100, 0);
    assert(O.front[0][0] > 0.05f && O.front[0][0] < 0.95f);
    run(150, 0);
    assert(O.front[0][0] == 1.0f && O.front[0][2] == 1.0f);   /* white */
    assert(NEAR(O.rear[0][0], up_srgb_to_linear(0.30f)) && O.rear[0][1] == 0.0f);   /* tail 30 % */

    /* blinking left replaces the base light on that side, smoothly */
    up_lights_set_signal(&L, UP_SIGNAL_LEFT, t, mm);
    float prev = O.front[0][2];
    for (int i = 0; i < 700; i++) {
        run(1, 0);
        assert(fabsf(O.front[0][2] - prev) < 0.05f);    /* blue channel = white part only */
        prev = O.front[0][2];
    }
    assert(O.front[0][2] == 0.0f);                      /* no white while blinking */
    assert(O.front[1][2] == 1.0f);                      /* right side keeps its light */

    /* switch off in the middle of a flash: fades out, no hard step */
    run(667 - (t - L.phase_t0_ms) % 667 + 200, 0);     /* full phase */
    up_lights_set_signal(&L, UP_SIGNAL_OFF, t, mm);
    prev = O.front[0][0];
    for (int i = 0; i < 300; i++) {
        run(1, 0);
        assert(fabsf(O.front[0][0] - prev) < 0.05f);
        prev = O.front[0][0];
    }
    assert(O.front[0][2] == 1.0f);                      /* white is back */
}

static void test_brake(void)
{
    t = 100000;
    mm = 0;
    up_lights_init(&L, t);
    up_lights_set_settings(&L, 100, true, 10);   /* 1.0 m/s² */
    run(2000, 3000);
    assert(!L.brake);

    /* coasting: -0.3 m/s² does not trigger */
    int32_t v = 3000;
    for (int i = 0; i < 1000; i++) {
        v = 3000 - (int32_t)(0.3f * i);
        run(1, v);
    }
    assert(!L.brake);

    /* braking with 2 m/s² */
    uint32_t t_start = t;
    int32_t v0 = v;
    while (!L.brake) {
        v = v0 - (int32_t)(2.0f * (t - t_start));
        run(1, v);
        assert(t - t_start < 400);
    }
    printf("brake light on after %u ms of braking\n", t - t_start);
    up_lights_output(&L, &O);
    assert(O.rear[0][0] < 0.05f);                       /* starts to rise, not hard on */
    run(100, v);
    assert(O.rear[0][0] == 1.0f && O.rear[1][0] == 1.0f && O.front[0][0] == 0.0f);
    assert(up_lights_status(&L) & UP_LIGHTS_BRAKE_ON);

    /* constant speed again: stays on for at least 1 s, then fades out */
    run(500, v);
    assert(L.brake);
    run(1000, v);
    assert(!L.brake);
    run(250, v);
    assert(O.rear[0][0] == 0.0f);

    /* disabled: no brake light */
    up_lights_set_settings(&L, 100, false, 10);
    t_start = t;
    v0 = v;
    for (int i = 0; i < 300; i++) {
        run(1, v0 - (int32_t)(3.0f * i));
    }
    assert(!L.brake);
}

int main(void)
{
    test_envelope();
    test_signal_and_timeout();
    test_smooth_switch_off_and_light();
    test_brake();
    printf("test_lights: all passed\n");
    return 0;
}
