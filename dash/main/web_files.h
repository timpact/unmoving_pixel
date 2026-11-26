/*
 * Web editor files embedded in the firmware (ADR 0013), generated at build
 * time from tools/preset-editor/ by tools/embed_web.py (gzipped).
 */
#ifndef WEB_FILES_H
#define WEB_FILES_H

#include <stddef.h>

typedef struct {
    const char *path;            /* "/index.html" */
    const char *mime;
    const unsigned char *data;   /* gzip */
    size_t size;
    const char *etag;            /* quoted, e.g. "\"3f2a…\"" */
} web_file_t;

extern const web_file_t web_files[];
extern const int web_file_count;

#endif /* WEB_FILES_H */
