/* Minimal in-house JSON for the control protocol: a growable writer and a
 * reader for the flat objects the protocol exchanges.
 *
 * Deliberately not a general JSON library. The reader walks only top-level
 * members of one object and skips nested values without interpreting them,
 * which is all a command line needs; the writer emits exactly one line, so a
 * reply is always a single newline-delimited record however odd the tags in a
 * file are. */
#ifndef KA_JSON_H
#define KA_JSON_H

#include "common.h"

/* --- Writer --- */

typedef struct {
    char *buf;  /* NUL-terminated; NULL until the first append */
    size_t len;
    size_t cap;
} JsonBuf;

void json_free(JsonBuf *j);
/* "" when nothing has been written, never NULL. */
const char *json_text(const JsonBuf *j);

void json_obj_begin(JsonBuf *j);
void json_obj_end(JsonBuf *j);
/* Arrays are opened by key inside an object and closed with json_arr_end. */
void json_arr_begin(JsonBuf *j, const char *key);
void json_arr_end(JsonBuf *j);

void json_kv_str(JsonBuf *j, const char *key, const char *value);
void json_kv_int(JsonBuf *j, const char *key, long long value);
/* Fixed 3-decimal output: positions stay exact enough to drive a progress bar
 * and never render as an exponent the client would have to parse loosely. */
void json_kv_num(JsonBuf *j, const char *key, double value);
void json_kv_bool(JsonBuf *j, const char *key, bool value);
void json_arr_str(JsonBuf *j, const char *value);

/* --- Reader --- */

/* Each returns false when the key is absent at the top level, when the object
 * is malformed, or when the value has the wrong type. `out` is untouched then.
 *
 * json_get_str writes at most n-1 bytes plus a NUL and decodes \" \\ \/ \b \f
 * \n \r \t and \uXXXX (as UTF-8, with surrogate pairs). */
bool json_get_str(const char *json, const char *key, char *out, size_t n);
bool json_get_num(const char *json, const char *key, double *out);
bool json_get_int(const char *json, const char *key, long long *out);
bool json_get_bool(const char *json, const char *key, bool *out);

#endif
