#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Flat key/value store; keys are "section/name" and serialized as INI
 * sections the same way QSettings did, so old nixamp.ini files remain
 * readable if copied over. */

typedef struct {
    char *key; /* "audio/volume" */
    char *val;
} Entry;

struct Config {
    char *path;
    Entry *entries;
    size_t count, cap;
    bool dirty;
};

static Entry *find_entry(Config *cfg, const char *key)
{
    for (size_t i = 0; i < cfg->count; i++)
        if (strcmp(cfg->entries[i].key, key) == 0)
            return &cfg->entries[i];
    return NULL;
}

void config_set(Config *cfg, const char *key, const char *value)
{
    Entry *e = find_entry(cfg, key);
    if (e) {
        if (strcmp(e->val, value) != 0) {
            free(e->val);
            e->val = ka_strdup(value);
            cfg->dirty = true;
        }
        return;
    }
    if (cfg->count == cfg->cap) {
        cfg->cap = cfg->cap ? cfg->cap * 2 : 32;
        cfg->entries = realloc(cfg->entries, cfg->cap * sizeof(Entry));
        if (!cfg->entries)
            abort();
    }
    cfg->entries[cfg->count].key = ka_strdup(key);
    cfg->entries[cfg->count].val = ka_strdup(value);
    cfg->count++;
    cfg->dirty = true;
}

static void load_file(Config *cfg)
{
    char *data = ka_read_file(cfg->path, NULL);
    if (!data)
        return;
    char section[128] = "";
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *s = ka_strip(line);
        if (!*s || *s == ';' || *s == '#')
            continue;
        if (*s == '[') {
            char *end = strchr(s, ']');
            if (end) {
                *end = '\0';
                snprintf(section, sizeof(section), "%s", s + 1);
            }
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = ka_strip(s);
        char *val = ka_strip(eq + 1);
        char *full = *section ? ka_asprintf("%s/%s", section, key)
                              : ka_strdup(key);
        config_set(cfg, full, val);
        free(full);
    }
    free(data);
    cfg->dirty = false;
}

static char *default_path(void)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg)
        return ka_asprintf("%s/kilix-amp/kilix-amp.ini", xdg);
    const char *home = getenv("HOME");
    if (!home)
        home = ".";
    return ka_asprintf("%s/.config/kilix-amp/kilix-amp.ini", home);
}

Config *config_open(const char *path)
{
    Config *cfg = calloc(1, sizeof(*cfg));
    if (!cfg)
        abort();
    cfg->path = path ? ka_strdup(path) : default_path();
    load_file(cfg);
    return cfg;
}

static int cmp_entry(const void *a, const void *b)
{
    return strcmp(((const Entry *)a)->key, ((const Entry *)b)->key);
}

bool config_sync(Config *cfg)
{
    if (!cfg->dirty)
        return true;
    char *dir = ka_dirname(cfg->path);
    ka_mkdirs(dir);
    free(dir);

    /* Sort so keys group by section prefix, then emit INI sections. */
    qsort(cfg->entries, cfg->count, sizeof(Entry), cmp_entry);

    FILE *f = fopen(cfg->path, "w");
    if (!f)
        return false;
    char cur_section[128] = "";
    for (size_t i = 0; i < cfg->count; i++) {
        const char *key = cfg->entries[i].key;
        const char *slash = strchr(key, '/');
        char section[128] = "";
        const char *name = key;
        if (slash) {
            snprintf(section, sizeof(section), "%.*s",
                     (int)(slash - key), key);
            name = slash + 1;
        }
        if (strcmp(section, cur_section) != 0) {
            fprintf(f, "%s[%s]\n", i ? "\n" : "", section);
            snprintf(cur_section, sizeof(cur_section), "%s", section);
        }
        fprintf(f, "%s=%s\n", name, cfg->entries[i].val);
    }
    fclose(f);
    cfg->dirty = false;
    return true;
}

void config_close(Config *cfg)
{
    if (!cfg)
        return;
    config_sync(cfg);
    for (size_t i = 0; i < cfg->count; i++) {
        free(cfg->entries[i].key);
        free(cfg->entries[i].val);
    }
    free(cfg->entries);
    free(cfg->path);
    free(cfg);
}

const char *config_get(Config *cfg, const char *key, const char *dflt)
{
    Entry *e = find_entry(cfg, key);
    return e ? e->val : dflt;
}

int config_get_int(Config *cfg, const char *key, int dflt)
{
    const char *v = config_get(cfg, key, NULL);
    if (!v)
        return dflt;
    char *end;
    long n = strtol(v, &end, 10);
    return (end == v) ? dflt : (int)n;
}

bool config_get_bool(Config *cfg, const char *key, bool dflt)
{
    const char *v = config_get(cfg, key, NULL);
    if (!v)
        return dflt;
    return ka_ieq(v, "true") || strcmp(v, "1") == 0 || ka_ieq(v, "yes");
}

double config_get_float(Config *cfg, const char *key, double dflt)
{
    const char *v = config_get(cfg, key, NULL);
    if (!v)
        return dflt;
    char *end;
    double d = strtod(v, &end);
    return (end == v) ? dflt : d;
}

void config_set_int(Config *cfg, const char *key, int value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value);
    config_set(cfg, key, buf);
}

void config_set_bool(Config *cfg, const char *key, bool value)
{
    config_set(cfg, key, value ? "true" : "false");
}

void config_set_float(Config *cfg, const char *key, double value)
{
    char buf[48];
    snprintf(buf, sizeof(buf), "%g", value);
    config_set(cfg, key, buf);
}

/* --- Convenience accessors --- */

int config_volume(Config *c) { return config_get_int(c, "audio/volume", 80); }
void config_set_volume(Config *c, int v) { config_set_int(c, "audio/volume", v); }
int config_balance(Config *c) { return config_get_int(c, "audio/balance", 0); }
void config_set_balance(Config *c, int v) { config_set_int(c, "audio/balance", v); }
bool config_shuffle(Config *c) { return config_get_bool(c, "playlist/shuffle", false); }
void config_set_shuffle(Config *c, bool v) { config_set_bool(c, "playlist/shuffle", v); }
int config_repeat(Config *c) { return config_get_int(c, "playlist/repeat", 0); }
void config_set_repeat(Config *c, int v) { config_set_int(c, "playlist/repeat", v); }
bool config_eq_enabled(Config *c) { return config_get_bool(c, "eq/enabled", false); }
void config_set_eq_enabled(Config *c, bool v) { config_set_bool(c, "eq/enabled", v); }
double config_eq_preamp(Config *c) { return config_get_float(c, "eq/preamp", 0.0); }
void config_set_eq_preamp(Config *c, double v) { config_set_float(c, "eq/preamp", v); }

void config_get_eq_bands(Config *c, float out[10])
{
    for (int i = 0; i < 10; i++) {
        char key[24];
        snprintf(key, sizeof(key), "eq/band%d", i);
        out[i] = (float)config_get_float(c, key, 0.0);
    }
}

void config_set_eq_bands(Config *c, const float bands[10])
{
    for (int i = 0; i < 10; i++) {
        char key[24];
        snprintf(key, sizeof(key), "eq/band%d", i);
        config_set_float(c, key, bands[i]);
    }
}

const char *config_skin_path(Config *c) { return config_get(c, "ui/skin_path", ""); }
void config_set_skin_path(Config *c, const char *v) { config_set(c, "ui/skin_path", v); }
bool config_double_size(Config *c) { return config_get_bool(c, "ui/double_size", false); }
void config_set_double_size(Config *c, bool v) { config_set_bool(c, "ui/double_size", v); }

double config_scale(Config *c)
{
    double dflt = config_double_size(c) ? 2.0 : 1.0;
    return config_get_float(c, "ui/scale", dflt);
}

void config_set_scale(Config *c, double v)
{
    config_set_float(c, "ui/scale", v);
}

bool config_get_window_pos(Config *c, const char *window, int *x, int *y)
{
    char kx[64], ky[64];
    snprintf(kx, sizeof(kx), "ui/%s_x", window);
    snprintf(ky, sizeof(ky), "ui/%s_y", window);
    if (!config_get(c, kx, NULL) || !config_get(c, ky, NULL))
        return false;
    *x = config_get_int(c, kx, 0);
    *y = config_get_int(c, ky, 0);
    return true;
}

void config_set_window_pos(Config *c, const char *window, int x, int y)
{
    char kx[64], ky[64];
    snprintf(kx, sizeof(kx), "ui/%s_x", window);
    snprintf(ky, sizeof(ky), "ui/%s_y", window);
    config_set_int(c, kx, x);
    config_set_int(c, ky, y);
}

bool config_eq_visible(Config *c) { return config_get_bool(c, "ui/eq_visible", false); }
void config_set_eq_visible(Config *c, bool v) { config_set_bool(c, "ui/eq_visible", v); }
bool config_pl_visible(Config *c) { return config_get_bool(c, "ui/pl_visible", true); }
void config_set_pl_visible(Config *c, bool v) { config_set_bool(c, "ui/pl_visible", v); }
int config_pl_width(Config *c) { return config_get_int(c, "ui/pl_width", 275); }
void config_set_pl_width(Config *c, int v) { config_set_int(c, "ui/pl_width", v); }
int config_pl_height(Config *c) { return config_get_int(c, "ui/pl_height", 232); }
void config_set_pl_height(Config *c, int v) { config_set_int(c, "ui/pl_height", v); }
bool config_ed_visible(Config *c) { return config_get_bool(c, "ui/ed_visible", false); }
void config_set_ed_visible(Config *c, bool v) { config_set_bool(c, "ui/ed_visible", v); }
int config_ed_width(Config *c) { return config_get_int(c, "ui/ed_width", 275); }
void config_set_ed_width(Config *c, int v) { config_set_int(c, "ui/ed_width", v); }
int config_ed_height(Config *c) { return config_get_int(c, "ui/ed_height", 300); }
void config_set_ed_height(Config *c, int v) { config_set_int(c, "ui/ed_height", v); }
