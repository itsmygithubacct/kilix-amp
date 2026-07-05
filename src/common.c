#include "common.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static void *xcheck(void *p)
{
    if (!p) {
        fputs("kilix-amp: out of memory\n", stderr);
        abort();
    }
    return p;
}

char *ka_strdup(const char *s)
{
    return xcheck(strdup(s ? s : ""));
}

char *ka_strndup(const char *s, size_t n)
{
    return xcheck(strndup(s ? s : "", n));
}

char *ka_asprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *out = NULL;
    if (vasprintf(&out, fmt, ap) < 0)
        out = NULL;
    va_end(ap);
    return xcheck(out);
}

char *ka_lower(char *s)
{
    for (char *p = s; *p; p++)
        *p = (char)tolower((unsigned char)*p);
    return s;
}

bool ka_ieq(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

const char *ka_basename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

char *ka_ext_lower(const char *path)
{
    const char *base = ka_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base)
        return ka_strdup("");
    return ka_lower(ka_strdup(dot));
}

char *ka_stem(const char *path)
{
    const char *base = ka_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base)
        return ka_strdup(base);
    return ka_strndup(base, (size_t)(dot - base));
}

char *ka_dirname(const char *path)
{
    const char *slash = strrchr(path, '/');
    if (!slash)
        return ka_strdup(".");
    if (slash == path)
        return ka_strdup("/");
    return ka_strndup(path, (size_t)(slash - path));
}

char *ka_path_join(const char *dir, const char *name)
{
    size_t dlen = strlen(dir);
    if (dlen == 0)
        return ka_strdup(name);
    if (dir[dlen - 1] == '/')
        return ka_asprintf("%s%s", dir, name);
    return ka_asprintf("%s/%s", dir, name);
}

char *ka_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = xcheck(malloc((size_t)sz + 1));
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = '\0';
    if (len)
        *len = (size_t)sz;
    return buf;
}

bool ka_is_file(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool ka_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool ka_mkdirs(const char *path)
{
    char *tmp = ka_strdup(path);
    bool ok = true;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && !ka_is_dir(tmp))
                ok = false;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && !ka_is_dir(tmp))
        ok = false;
    free(tmp);
    return ok;
}

char *ka_strip(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}
