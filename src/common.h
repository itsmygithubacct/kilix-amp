/* kilix-amp - shared basic types and helpers. */
#ifndef KA_COMMON_H
#define KA_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Same layout as SDL_Rect so pointers can be cast where SDL is present. */
typedef struct {
    int x, y, w, h;
} KRect;

typedef struct {
    uint8_t r, g, b;
} KColor;

#define KRECT(X, Y, W, H) ((KRect){(X), (Y), (W), (H)})

#define KA_MIN(a, b) ((a) < (b) ? (a) : (b))
#define KA_MAX(a, b) ((a) > (b) ? (a) : (b))
#define KA_CLAMP(v, lo, hi) KA_MAX((lo), KA_MIN((hi), (v)))
#define KA_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* strdup that aborts on OOM (allocation failure is unrecoverable here). */
char *ka_strdup(const char *s);
char *ka_strndup(const char *s, size_t n);
/* printf-style heap string */
char *ka_asprintf(const char *fmt, ...);
/* In-place ASCII lowercase; returns s. */
char *ka_lower(char *s);
/* Case-insensitive string equality. */
bool ka_ieq(const char *a, const char *b);
/* File path helpers (return pointers into `path` or heap strings). */
const char *ka_basename(const char *path);
/* Lowercased extension including the dot ("" if none); heap string. */
char *ka_ext_lower(const char *path);
/* Heap copy of path without its extension, basename only. */
char *ka_stem(const char *path);
/* Directory part of path as heap string ("." if none). */
char *ka_dirname(const char *path);
/* Join dir + "/" + name as heap string. */
char *ka_path_join(const char *dir, const char *name);
/* Whole-file read; returns malloc'd buffer (NUL-terminated), sets *len. NULL on error. */
char *ka_read_file(const char *path, size_t *len);
bool ka_is_file(const char *path);
bool ka_is_dir(const char *path);
/* mkdir -p. Returns true on success or if it already exists. */
bool ka_mkdirs(const char *path);
/* Trim leading/trailing ASCII whitespace in place; returns start pointer. */
char *ka_strip(char *s);

#endif
