#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

/* --- Writer --- */

static void jb_reserve(JsonBuf *j, size_t extra)
{
    size_t need = j->len + extra + 1;
    if (need <= j->cap)
        return;
    size_t cap = j->cap ? j->cap : 256;
    while (cap < need)
        cap *= 2;
    char *buf = realloc(j->buf, cap);
    if (!buf)
        abort();
    j->buf = buf;
    j->cap = cap;
}

static void jb_putc(JsonBuf *j, char c)
{
    jb_reserve(j, 1);
    j->buf[j->len++] = c;
    j->buf[j->len] = '\0';
}

static void jb_puts(JsonBuf *j, const char *s)
{
    size_t n = strlen(s);
    jb_reserve(j, n);
    memcpy(j->buf + j->len, s, n);
    j->len += n;
    j->buf[j->len] = '\0';
}

/* A comma is needed unless we are at the very start or just after an opening
 * brace/bracket. Tracking the last byte is enough for the shapes we emit. */
static void jb_sep(JsonBuf *j)
{
    if (!j->len)
        return;
    char last = j->buf[j->len - 1];
    if (last == '{' || last == '[')
        return;
    jb_putc(j, ',');
}

/* Length of the valid UTF-8 sequence starting at s, or 0 if it is not one.
 * Rejects overlong forms, surrogates, and anything past U+10FFFF so the writer
 * never emits a byte sequence a strict JSON reader would reject. */
static int utf8_len(const unsigned char *s, size_t avail)
{
    unsigned char c = s[0];
    if (c < 0x80)
        return 1;
    int n;
    unsigned long cp;
    if ((c & 0xE0) == 0xC0) {
        n = 2;
        cp = c & 0x1Fu;
    } else if ((c & 0xF0) == 0xE0) {
        n = 3;
        cp = c & 0x0Fu;
    } else if ((c & 0xF8) == 0xF0) {
        n = 4;
        cp = c & 0x07u;
    } else {
        return 0;
    }
    if (avail < (size_t)n)
        return 0;
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80)
            return 0;
        cp = (cp << 6) | (s[i] & 0x3Fu);
    }
    if ((n == 2 && cp < 0x80) || (n == 3 && cp < 0x800) ||
        (n == 4 && cp < 0x10000))
        return 0; /* overlong */
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        return 0;
    return n;
}

static void jb_escaped(JsonBuf *j, const char *value)
{
    const unsigned char *s = (const unsigned char *)value;
    size_t len = strlen(value);
    jb_putc(j, '"');
    for (size_t i = 0; i < len;) {
        unsigned char c = s[i];
        if (c == '"' || c == '\\') {
            jb_putc(j, '\\');
            jb_putc(j, (char)c);
            i++;
            continue;
        }
        if (c >= 0x20 && c < 0x7F) {
            jb_putc(j, (char)c);
            i++;
            continue;
        }
        if (c < 0x20) {
            /* Control bytes, including the newline that would otherwise split
             * a reply across two records. */
            switch (c) {
            case '\b': jb_puts(j, "\\b"); break;
            case '\f': jb_puts(j, "\\f"); break;
            case '\n': jb_puts(j, "\\n"); break;
            case '\r': jb_puts(j, "\\r"); break;
            case '\t': jb_puts(j, "\\t"); break;
            default: {
                char esc[7];
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                jb_puts(j, esc);
                break;
            }
            }
            i++;
            continue;
        }
        int n = utf8_len(s + i, len - i);
        if (n == 0) {
            /* A file name is a byte string, not necessarily UTF-8. Substitute
             * rather than emit a sequence no JSON reader would accept. */
            jb_puts(j, "\\ufffd");
            i++;
            continue;
        }
        jb_reserve(j, (size_t)n);
        memcpy(j->buf + j->len, s + i, (size_t)n);
        j->len += (size_t)n;
        j->buf[j->len] = '\0';
        i += (size_t)n;
    }
    jb_putc(j, '"');
}

void json_free(JsonBuf *j)
{
    free(j->buf);
    j->buf = NULL;
    j->len = j->cap = 0;
}

const char *json_text(const JsonBuf *j)
{
    return j->buf ? j->buf : "";
}

void json_obj_begin(JsonBuf *j)
{
    jb_sep(j);
    jb_putc(j, '{');
}

void json_obj_end(JsonBuf *j)
{
    jb_putc(j, '}');
}

void json_arr_begin(JsonBuf *j, const char *key)
{
    jb_sep(j);
    jb_escaped(j, key);
    jb_putc(j, ':');
    jb_putc(j, '[');
}

void json_arr_end(JsonBuf *j)
{
    jb_putc(j, ']');
}

static void jb_key(JsonBuf *j, const char *key)
{
    jb_sep(j);
    jb_escaped(j, key);
    jb_putc(j, ':');
}

void json_kv_str(JsonBuf *j, const char *key, const char *value)
{
    jb_key(j, key);
    jb_escaped(j, value ? value : "");
}

void json_kv_int(JsonBuf *j, const char *key, long long value)
{
    char num[32];
    snprintf(num, sizeof(num), "%lld", value);
    jb_key(j, key);
    jb_puts(j, num);
}

void json_kv_num(JsonBuf *j, const char *key, double value)
{
    char num[64];
    if (!(value == value) || value > 1e15 || value < -1e15)
        value = 0.0; /* NaN/inf are not JSON; report the neutral value */
    snprintf(num, sizeof(num), "%.3f", value);
    jb_key(j, key);
    jb_puts(j, num);
}

void json_kv_bool(JsonBuf *j, const char *key, bool value)
{
    jb_key(j, key);
    jb_puts(j, value ? "true" : "false");
}

void json_kv_null(JsonBuf *j, const char *key)
{
    jb_key(j, key);
    jb_puts(j, "null");
}

void json_arr_str(JsonBuf *j, const char *value)
{
    jb_sep(j);
    jb_escaped(j, value ? value : "");
}

/* --- Reader --- */

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        p++;
    return p;
}

/* Past a JSON string starting at the opening quote, or NULL if unterminated. */
static const char *skip_string(const char *p)
{
    if (*p != '"')
        return NULL;
    p++;
    while (*p) {
        if (*p == '\\') {
            if (!p[1])
                return NULL;
            p += 2;
            continue;
        }
        if (*p == '"')
            return p + 1;
        p++;
    }
    return NULL;
}

/* Past any single JSON value, or NULL if malformed. */
static const char *skip_value(const char *p)
{
    p = skip_ws(p);
    if (*p == '"')
        return skip_string(p);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (*p) {
            if (*p == '"') {
                const char *next = skip_string(p);
                if (!next)
                    return NULL;
                p = next;
                continue;
            }
            if (*p == '{' || *p == '[')
                depth++;
            else if (*p == '}' || *p == ']') {
                depth--;
                if (depth == 0)
                    return p + 1;
            }
            p++;
        }
        return NULL;
    }
    const char *start = p;
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != ' ' &&
           *p != '\t' && *p != '\r' && *p != '\n')
        p++;
    return p == start ? NULL : p;
}

/* Locate the value of a top-level key. Returns a pointer to the first byte of
 * the value, or NULL. Nested objects are skipped whole, so an "index" inside a
 * sub-object is never mistaken for the top-level one. */
static const char *find_value(const char *json, const char *key)
{
    if (!json || !key)
        return NULL;
    const char *p = skip_ws(json);
    if (*p != '{')
        return NULL;
    p++;
    size_t keylen = strlen(key);
    for (;;) {
        p = skip_ws(p);
        if (*p == '}' || !*p)
            return NULL;
        if (*p != '"')
            return NULL;
        const char *name = p + 1;
        const char *after = skip_string(p);
        if (!after)
            return NULL;
        size_t namelen = (size_t)(after - 1 - name);
        p = skip_ws(after);
        if (*p != ':')
            return NULL;
        p = skip_ws(p + 1);
        /* Keys in this protocol are plain ASCII, so a byte compare is exact. */
        if (namelen == keylen && memcmp(name, key, keylen) == 0)
            return p;
        p = skip_value(p);
        if (!p)
            return NULL;
        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == '}')
            return NULL;
        return NULL;
    }
}

static int hex4(const char *p, unsigned *out)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')
            v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v |= (unsigned)(c - 'A' + 10);
        else
            return 0;
    }
    *out = v;
    return 1;
}

typedef struct { const char *at, *end; } RequestParser;

static void request_ws(RequestParser *parser)
{
    while (parser->at < parser->end && strchr(" \t\r\n", *parser->at) != NULL) ++parser->at;
}

static bool request_string(RequestParser *parser, bool key)
{
    if (parser->at == parser->end || *parser->at++ != '"') return false;
    while (parser->at < parser->end) {
        unsigned char byte = (unsigned char)*parser->at++;
        if (byte == '"') return true;
        if (byte < 0x20u || (key && (byte == '\\' || byte >= 0x7fu))) return false;
        if (byte == '\\') {
            if (parser->at == parser->end) return false;
            char escape = *parser->at++;
            if (escape == 'u') {
                unsigned value;
                if (parser->end - parser->at < 4 || !hex4(parser->at, &value) || value == 0u) return false;
                parser->at += 4;
                if (value >= 0xd800u && value <= 0xdbffu) {
                    unsigned low;
                    if (parser->end - parser->at < 6 || parser->at[0] != '\\' || parser->at[1] != 'u'
                        || !hex4(parser->at + 2, &low) || low < 0xdc00u || low > 0xdfffu) return false;
                    parser->at += 6;
                } else if (value >= 0xdc00u && value <= 0xdfffu) return false;
            } else if (strchr("\"\\/bfnrt", escape) == NULL) return false;
        } else if (byte >= 0x80u) {
            --parser->at;
            int length = utf8_len((const unsigned char *)parser->at, (size_t)(parser->end - parser->at));
            if (length == 0) return false;
            parser->at += length;
        }
    }
    return false;
}

static bool request_value(RequestParser *parser, unsigned int depth)
{
    request_ws(parser);
    if (parser->at == parser->end || depth > 16u) return false;
    char kind = *parser->at;
    if (kind == '"') return request_string(parser, false);
    if (kind == '{' || kind == '[') {
        ++parser->at;
        const char *keys[64]; size_t sizes[64], count = 0u;
        char end = kind == '{' ? '}' : ']';
        request_ws(parser);
        if (parser->at < parser->end && *parser->at == end) { ++parser->at; return true; }
        for (;;) {
            request_ws(parser);
            if (kind == '{') {
                const char *key = parser->at;
                if (count == 64u || !request_string(parser, true)) return false;
                size_t size = (size_t)(parser->at - key);
                if (size < 3u || size > 65u) return false;
                for (size_t i = 0u; i < count; ++i)
                    if (sizes[i] == size && !memcmp(keys[i], key, size)) return false;
                keys[count] = key; sizes[count++] = size;
                request_ws(parser);
                if (parser->at == parser->end || *parser->at++ != ':') return false;
            }
            if (!request_value(parser, depth + 1u)) return false;
            request_ws(parser);
            if (parser->at == parser->end) return false;
            char next = *parser->at++;
            if (next == end) return true;
            if (next != ',') return false;
        }
    }
    const char *constants[] = {"true", "false", "null"};
    for (size_t i = 0u; i < 3u; ++i) {
        size_t length = strlen(constants[i]);
        if ((size_t)(parser->end - parser->at) >= length && !memcmp(parser->at, constants[i], length)) {
            parser->at += length; return true;
        }
    }
    const char *start = parser->at;
    if (*parser->at == '-') ++parser->at;
    if (parser->at == parser->end) return false;
    if (*parser->at == '0') ++parser->at;
    else {
        if (*parser->at < '1' || *parser->at > '9') return false;
        do { ++parser->at; } while (parser->at < parser->end && *parser->at >= '0' && *parser->at <= '9');
    }
    if (parser->at < parser->end && *parser->at == '.') {
        ++parser->at;
        const char *fraction = parser->at;
        while (parser->at < parser->end && *parser->at >= '0' && *parser->at <= '9') ++parser->at;
        if (parser->at == fraction) return false;
    }
    if (parser->at < parser->end && (*parser->at == 'e' || *parser->at == 'E')) {
        ++parser->at;
        if (parser->at < parser->end && (*parser->at == '+' || *parser->at == '-')) ++parser->at;
        const char *exponent = parser->at;
        while (parser->at < parser->end && *parser->at >= '0' && *parser->at <= '9') ++parser->at;
        if (parser->at == exponent) return false;
    }
    char *number_end;
    double number = strtod(start, &number_end);
    return number_end == parser->at && isfinite(number);
}

bool json_validate_request(const char *json)
{
    if (json == NULL) return false;
    size_t length = strnlen(json, 8193u);
    if (length > 8192u) return false;
    RequestParser parser = {json, json + length};
    request_ws(&parser);
    if (parser.at == parser.end || *parser.at != '{' || !request_value(&parser, 0u)) return false;
    request_ws(&parser);
    return parser.at == parser.end;
}

bool json_has_key(const char *json, const char *key)
{
    return find_value(json, key) != NULL;
}

bool json_get_str_exact(const char *json, const char *key, char *out, size_t n)
{
    if (json == NULL || out == NULL || !json_validate_request(json)) return false;
    size_t length = strlen(json) + 1u;
    char *copy = malloc(length);
    if (copy == NULL) return false;
    bool valid = json_get_str(json, key, copy, length);
    if (valid) {
        length = strlen(copy) + 1u;
        if (length > n) valid = false;
        else memcpy(out, copy, length);
    }
    free(copy);
    return valid;
}

static size_t put_utf8(char *out, size_t n, size_t at, unsigned long cp)
{
    unsigned char seq[4];
    int len;
    if (cp < 0x80) {
        seq[0] = (unsigned char)cp;
        len = 1;
    } else if (cp < 0x800) {
        seq[0] = (unsigned char)(0xC0 | (cp >> 6));
        seq[1] = (unsigned char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        seq[0] = (unsigned char)(0xE0 | (cp >> 12));
        seq[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        seq[2] = (unsigned char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        seq[0] = (unsigned char)(0xF0 | (cp >> 18));
        seq[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
        seq[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
        seq[3] = (unsigned char)(0x80 | (cp & 0x3F));
        len = 4;
    }
    for (int i = 0; i < len; i++) {
        if (at + 1 >= n)
            break;
        out[at++] = (char)seq[i];
    }
    return at;
}

bool json_get_str(const char *json, const char *key, char *out, size_t n)
{
    const char *p = find_value(json, key);
    if (!p || *p != '"' || n == 0)
        return false;
    p++;
    size_t at = 0;
    while (*p && *p != '"') {
        if (*p != '\\') {
            if (at + 1 < n)
                out[at++] = *p;
            p++;
            continue;
        }
        p++;
        switch (*p) {
        case '"': case '\\': case '/':
            if (at + 1 < n)
                out[at++] = *p;
            p++;
            break;
        case 'b': if (at + 1 < n) out[at++] = '\b'; p++; break;
        case 'f': if (at + 1 < n) out[at++] = '\f'; p++; break;
        case 'n': if (at + 1 < n) out[at++] = '\n'; p++; break;
        case 'r': if (at + 1 < n) out[at++] = '\r'; p++; break;
        case 't': if (at + 1 < n) out[at++] = '\t'; p++; break;
        case 'u': {
            unsigned cp;
            if (!hex4(p + 1, &cp))
                return false;
            p += 5;
            unsigned long full = cp;
            if (cp >= 0xD800 && cp <= 0xDBFF && p[0] == '\\' && p[1] == 'u') {
                unsigned lo;
                if (hex4(p + 2, &lo) && lo >= 0xDC00 && lo <= 0xDFFF) {
                    full = 0x10000UL + (((unsigned long)cp - 0xD800) << 10) +
                           (lo - 0xDC00);
                    p += 6;
                }
            }
            if (full >= 0xD800 && full <= 0xDFFF)
                full = 0xFFFD; /* lone surrogate */
            at = put_utf8(out, n, at, full);
            break;
        }
        default:
            return false;
        }
    }
    if (*p != '"')
        return false;
    out[at] = '\0';
    return true;
}

bool json_get_num(const char *json, const char *key, double *out)
{
    const char *p = find_value(json, key);
    if (!p)
        return false;
    char *end = NULL;
    double v = strtod(p, &end);
    if (!end || end == p || !isfinite(v) || (*end && strchr(",}] \t\r\n", *end) == NULL))
        return false;
    *out = v;
    return true;
}

bool json_get_int(const char *json, const char *key, long long *out)
{
    const char *p = find_value(json, key);
    if (!p || (*p != '-' && (*p < '0' || *p > '9'))) return false;
    char *end;
    errno = 0;
    long long value = strtoll(p, &end, 10);
    if (errno == ERANGE || end == p || (*end && strchr(",}] \t\r\n", *end) == NULL)) return false;
    *out = value;
    return true;
}

bool json_get_bool(const char *json, const char *key, bool *out)
{
    const char *p = find_value(json, key);
    if (!p)
        return false;
    if (strncmp(p, "true", 4) == 0) {
        *out = true;
        return true;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = false;
        return true;
    }
    return false;
}
