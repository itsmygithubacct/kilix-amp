#include "filedialog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dialog.h"

static bool have_zenity(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = system("command -v zenity >/dev/null 2>&1") == 0 ? 1 : 0;
    return cached == 1;
}

/* Run a zenity command; returns trimmed stdout as heap string, NULL if
 * cancelled/failed. */
static char *run_zenity(const char *cmd)
{
    FILE *p = popen(cmd, "r");
    if (!p)
        return NULL;

    size_t len = 0, cap = 8192;
    char *buf = malloc(cap);
    if (!buf)
        abort();
    bool read_ok = true;
    for (;;) {
        if (len + 1 == cap) {
            if (cap > SIZE_MAX / 2)
                abort();
            cap *= 2;
            char *grown = realloc(buf, cap);
            if (!grown)
                abort();
            buf = grown;
        }
        size_t got = fread(buf + len, 1, cap - len - 1, p);
        len += got;
        if (got == 0) {
            if (ferror(p))
                read_ok = false;
            break;
        }
    }
    int rc = pclose(p);
    if (!read_ok || rc != 0 || len == 0) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    char *s = ka_strip(buf);
    char *result = *s ? ka_strdup(s) : NULL;
    free(buf);
    return result;
}

static char *shell_quote(const char *s)
{
    /* single-quote, escaping embedded quotes */
    size_t len = strlen(s);
    char *out = malloc(len * 4 + 3);
    if (!out)
        abort();
    char *p = out;
    *p++ = '\'';
    for (const char *c = s; *c; c++) {
        if (*c == '\'') {
            memcpy(p, "'\\''", 4);
            p += 4;
        } else {
            *p++ = *c;
        }
    }
    *p++ = '\'';
    *p = '\0';
    return out;
}

static char *fallback_prompt(const char *title, const char *initial)
{
    DialogField fields[1] = {
        dialog_field_text("path", "PATH:", initial ? initial : ""),
    };
    if (!dialog_run(title, fields, 1))
        return NULL;
    const char *path = dialog_get_str(fields, 1, "path", "");
    return *path ? ka_strdup(path) : NULL;
}

/* zenity --filename fragment so the picker opens IN ~/Music (trailing slash),
 * or "" when $HOME is unset. Caller frees. */
static char *default_dir_arg(void)
{
    const char *home = getenv("HOME");
    if (!home || !*home)
        return ka_strdup("");
    char *dir = ka_asprintf("%s/Music/", home);
    char *q = shell_quote(dir);
    char *arg = ka_asprintf("--filename=%s ", q);
    free(q);
    free(dir);
    return arg;
}

char **filedialog_open_files(const char *title, int *count)
{
    *count = 0;
    char *result = NULL;
    if (have_zenity()) {
        char *qt = shell_quote(title);
        char *dd = default_dir_arg();
        char *cmd = ka_asprintf(
            "zenity --file-selection --multiple --separator='\\n' %s"
            "--title=%s 2>/dev/null",
            dd, qt);
        result = run_zenity(cmd);
        free(cmd);
        free(dd);
        free(qt);
    } else {
        result = fallback_prompt(title, "");
    }
    if (!result)
        return NULL;

    /* Split on newlines */
    char **files = NULL;
    int n = 0, cap = 0;
    char *save = NULL;
    for (char *line = strtok_r(result, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *s = ka_strip(line);
        if (!*s)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 8;
            files = realloc(files, (size_t)cap * sizeof(char *));
            if (!files)
                abort();
        }
        files[n++] = ka_strdup(s);
    }
    free(result);
    *count = n;
    return files;
}

char *filedialog_open_file(const char *title)
{
    if (have_zenity()) {
        char *qt = shell_quote(title);
        char *dd = default_dir_arg();
        char *cmd = ka_asprintf(
            "zenity --file-selection %s--title=%s 2>/dev/null", dd, qt);
        char *result = run_zenity(cmd);
        free(cmd);
        free(dd);
        free(qt);
        return result;
    }
    return fallback_prompt(title, "");
}

char *filedialog_save_file(const char *title, const char *suggested)
{
    if (have_zenity()) {
        char *qt = shell_quote(title);
        char *qs = shell_quote(suggested ? suggested : "");
        char *cmd = ka_asprintf(
            "zenity --file-selection --save --confirm-overwrite "
            "--filename=%s --title=%s 2>/dev/null",
            qs, qt);
        char *result = run_zenity(cmd);
        free(cmd);
        free(qt);
        free(qs);
        return result;
    }
    return fallback_prompt(title, suggested);
}

char *filedialog_choose_dir(const char *title)
{
    if (have_zenity()) {
        char *qt = shell_quote(title);
        char *cmd = ka_asprintf(
            "zenity --file-selection --directory --title=%s 2>/dev/null",
            qt);
        char *result = run_zenity(cmd);
        free(cmd);
        free(qt);
        return result;
    }
    return fallback_prompt(title, "");
}
