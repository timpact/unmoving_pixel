/*
 * LVGL configuration for dash (ADR 0010).
 *
 * Shared by the ESP-IDF firmware and the PC simulator, so that the preview
 * matches the device. Options not listed here use the LVGL defaults
 * (see lv_conf_internal.h of LVGL 9.3).
 *
 * The simulator defines UP_SIM=1.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/* 480×800 MIPI-DSI panel, RGB565 */
#define LV_COLOR_DEPTH 16

/* Memory: LVGL's own allocator. On the device this pool is placed in PSRAM
 * by the port; the simulator just uses a static array. */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (1024 * 1024U)

#define LV_DEF_REFR_PERIOD 16   /* ms, ~60 Hz */
#define LV_DPI_DEF 200

/* Fonts: Montserrat everywhere.
 * - Text sizes with icons (18/22/34 px): built-in Montserrat (ASCII + LV_SYMBOL_*),
 *   with Tiny TTF of the same size as fallback for umlauts and other Latin-1 glyphs.
 * - Large sizes (60/130 px): Tiny TTF only (digits and units, no icons). */
#define LV_USE_TINY_TTF 1
#define LV_TINY_TTF_FILE_SUPPORT 0
#define LV_TINY_TTF_CACHE_GLYPH_CNT 256
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_34 1
#define LV_FONT_DEFAULT &lv_font_montserrat_22

/* Widgets used by the GUI */
#define LV_USE_ARC      1
#define LV_USE_BUTTON   1
#define LV_USE_CHART    1
#define LV_USE_DROPDOWN 1
#define LV_USE_LABEL    1
#define LV_USE_SLIDER   1
#define LV_USE_SWITCH   1
#define LV_USE_TABVIEW  1
#define LV_USE_QRCODE   1
#define LV_USE_TEXTAREA 1
#define LV_USE_KEYBOARD 1
#define LV_USE_BUTTONMATRIX 1
#define LV_USE_ROLLER   1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL   1
#define LV_USE_ASSERT_MALLOC 1

#define LV_BUILD_EXAMPLES 0

#if defined(UP_SIM) && UP_SIM
    #define LV_USE_SDL 1
    #define LV_SDL_INCLUDE_PATH <SDL2/SDL.h>
    #define LV_SDL_RENDER_MODE LV_DISPLAY_RENDER_MODE_DIRECT
    #define LV_SDL_BUF_COUNT 1
    #define LV_SDL_ACCELERATED 1
    #define LV_SDL_FULLSCREEN 0
    #define LV_SDL_DIRECT_EXIT 1
    #define LV_SDL_MOUSEWHEEL_MODE LV_SDL_MOUSEWHEEL_MODE_ENCODER
#endif

#endif /* LV_CONF_H */
