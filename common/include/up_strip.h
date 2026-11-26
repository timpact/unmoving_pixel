/*
 * Strip layout of the two LED strips (ADR 0018), shared by core and dash.
 *
 *   front end │ main part (pattern, faces the ground) │ rear end
 */
#ifndef UP_STRIP_H
#define UP_STRIP_H

#include <stdint.h>

#define UP_STRIP_LEDS_PER_M  144
#define UP_STRIP_PITCH_UM    6944   /* 1000 mm / 144 */

#define UP_STRIP_MAIN_MIN    36     /* 0.25 m */
#define UP_STRIP_MAIN_MAX    144    /* 1.0 m */
#define UP_STRIP_MAIN_DEF    72     /* 0.5 m */
#define UP_STRIP_END_MAX     8
#define UP_STRIP_END_DEF     4

/* layout_flags (ADR 0004 SETTINGS / SET_STRIP_LAYOUT) */
#define UP_LAYOUT_LEFT_DATA_FRONT  0x01   /* left strip: data input at the front end */
#define UP_LAYOUT_RIGHT_DATA_FRONT 0x02

/* Length of the main part in mm (rounded) */
#define UP_STRIP_MAIN_MM(leds) ((uint16_t)(((uint32_t)(leds) * UP_STRIP_PITCH_UM + 500u) / 1000u))

#endif /* UP_STRIP_H */
