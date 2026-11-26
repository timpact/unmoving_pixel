/* Minimal PNG writer/reader on top of zlib (see png.h). */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t get32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int write_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, type, 4);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    if (len > 0) {
        crc = crc32(crc, data, len);
    }
    uint8_t c[4];
    put32(c, (uint32_t)crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) &&
                   fwrite(c, 1, 4, f) == 4
               ? 0
               : -1;
}

int png_write_rgb(const char *path, int width, int height, const uint8_t *rgb)
{
    const size_t row = (size_t)width * 3;
    const size_t raw_len = (row + 1) * (size_t)height;
    uint8_t *raw = malloc(raw_len);
    if (raw == NULL) {
        return -1;
    }
    for (int y = 0; y < height; y++) {
        raw[y * (row + 1)] = 0;   /* filter type 0 (none) */
        memcpy(raw + y * (row + 1) + 1, rgb + y * row, row);
    }
    uLongf z_len = compressBound(raw_len);
    uint8_t *z = malloc(z_len);
    if (z == NULL || compress2(z, &z_len, raw, raw_len, 9) != Z_OK) {
        free(raw);
        free(z);
        return -1;
    }
    free(raw);

    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        free(z);
        return -1;
    }
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)width);
    put32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 2;    /* colour type RGB */
    ihdr[10] = 0;   /* compression */
    ihdr[11] = 0;   /* filter */
    ihdr[12] = 0;   /* no interlace */
    int ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) == 0 &&
             write_chunk(f, "IDAT", z, (uint32_t)z_len) == 0 && write_chunk(f, "IEND", NULL, 0) == 0;
    fclose(f);
    free(z);
    return ok ? 0 : -1;
}

int png_read_rgb(const char *path, int *width, int *height, uint8_t **rgb)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)size);
    if (buf == NULL || fread(buf, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        free(buf);
        return -1;
    }
    fclose(f);

    int w = 0, h = 0;
    uint8_t *z = NULL;
    size_t z_len = 0;
    long pos = 8;
    while (pos + 12 <= size) {
        uint32_t len = get32(buf + pos);
        const uint8_t *type = buf + pos + 4;
        const uint8_t *data = buf + pos + 8;
        if (pos + 12 + (long)len > size) {
            break;
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            w = (int)get32(data);
            h = (int)get32(data + 4);
            if (data[8] != 8 || data[9] != 2) {
                break;   /* unsupported format */
            }
        } else if (memcmp(type, "IDAT", 4) == 0) {
            uint8_t *nz = realloc(z, z_len + len);
            if (nz == NULL) {
                break;
            }
            z = nz;
            memcpy(z + z_len, data, len);
            z_len += len;
        }
        pos += 12 + len;
    }
    free(buf);
    if (w <= 0 || h <= 0 || z == NULL) {
        free(z);
        return -1;
    }

    const size_t row = (size_t)w * 3;
    uLongf raw_len = (row + 1) * (size_t)h;
    uint8_t *raw = malloc(raw_len);
    uint8_t *out = malloc(row * (size_t)h);
    int ok = raw && out && uncompress(raw, &raw_len, z, z_len) == Z_OK && raw_len == (row + 1) * (size_t)h;
    free(z);
    for (int y = 0; ok && y < h; y++) {
        if (raw[y * (row + 1)] != 0) {
            ok = 0;   /* only filter type 0 is supported */
            break;
        }
        memcpy(out + y * row, raw + y * (row + 1) + 1, row);
    }
    free(raw);
    if (!ok) {
        free(out);
        return -1;
    }
    *width = w;
    *height = h;
    *rgb = out;
    return 0;
}
