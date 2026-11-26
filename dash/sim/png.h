/*
 * Minimal PNG writer/reader for the simulator (ADR 0016).
 *
 * Writes 8-bit RGB PNGs (one IDAT chunk, filter type 0) using zlib.
 * The reader only supports files written by png_write_rgb().
 */
#ifndef SIM_PNG_H
#define SIM_PNG_H

#include <stdint.h>

/* rgb: width × height × 3 bytes. Returns 0 on success. */
int png_write_rgb(const char *path, int width, int height, const uint8_t *rgb);

/* Reads a PNG written by png_write_rgb(). *rgb is malloc'ed (free it).
 * Returns 0 on success. */
int png_read_rgb(const char *path, int *width, int *height, uint8_t **rgb);

#endif /* SIM_PNG_H */
