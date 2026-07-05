#include "skin.h"

#include <SDL_image.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zip.h"

void skin_colors_init(SkinColors *sc)
{
    memcpy(sc->viscolors, DEFAULT_VISCOLORS, sizeof(sc->viscolors));
    sc->pl_normal = (KColor){0x00, 0xFF, 0x00};
    sc->pl_current = (KColor){0xFF, 0xFF, 0xFF};
    sc->pl_normalbg = (KColor){0x00, 0x00, 0x00};
    sc->pl_selectedbg = (KColor){0x00, 0x00, 0xFF};
    snprintf(sc->pl_font, sizeof(sc->pl_font), "%s", DEFAULT_PL_FONT);
}

void skin_colors_parse_viscolor(SkinColors *sc, const char *text)
{
    /* 24 lines of "R,G,B // comment" */
    char *copy = ka_strdup(text);
    int count = 0;
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save);
         line && count < VISCOLOR_COUNT;
         line = strtok_r(NULL, "\n", &save)) {
        char *comment = strstr(line, "//");
        if (comment)
            *comment = '\0';
        int r, g, b;
        if (sscanf(line, " %d , %d , %d", &r, &g, &b) == 3) {
            sc->viscolors[count].r = (uint8_t)r;
            sc->viscolors[count].g = (uint8_t)g;
            sc->viscolors[count].b = (uint8_t)b;
            count++;
        }
    }
    free(copy);
}

/* Parse "#RRGGBB" / "RRGGBB" / "#RGB"; returns false if malformed. */
static bool parse_hex_color(const char *s, KColor *out)
{
    if (*s == '#')
        s++;
    size_t n = strlen(s);
    unsigned v;
    if (n == 6 && sscanf(s, "%6x", &v) == 1) {
        *out = (KColor){(uint8_t)(v >> 16), (uint8_t)(v >> 8 & 0xFF),
                        (uint8_t)(v & 0xFF)};
        return true;
    }
    if (n == 3 && sscanf(s, "%3x", &v) == 1) {
        uint8_t r = (uint8_t)(v >> 8 & 0xF), g = (uint8_t)(v >> 4 & 0xF),
                b = (uint8_t)(v & 0xF);
        *out = (KColor){(uint8_t)(r * 17), (uint8_t)(g * 17),
                        (uint8_t)(b * 17)};
        return true;
    }
    return false;
}

void skin_colors_parse_pledit(SkinColors *sc, const char *text)
{
    char *copy = ka_strdup(text);
    char *save = NULL;
    for (char *line = strtok_r(copy, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *s = ka_strip(line);
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = ka_lower(ka_strip(s));
        char *val = ka_strip(eq + 1);
        if (strcmp(key, "font") == 0) {
            snprintf(sc->pl_font, sizeof(sc->pl_font), "%s", val);
            continue;
        }
        KColor c;
        if (!parse_hex_color(val, &c))
            continue;
        if (strcmp(key, "normal") == 0)
            sc->pl_normal = c;
        else if (strcmp(key, "current") == 0)
            sc->pl_current = c;
        else if (strcmp(key, "normalbg") == 0)
            sc->pl_normalbg = c;
        else if (strcmp(key, "selectedbg") == 0)
            sc->pl_selectedbg = c;
    }
    free(copy);
}

KColor skin_colors_vis(const SkinColors *sc, int index)
{
    return sc->viscolors[KA_CLAMP(index, 0, VISCOLOR_COUNT - 1)];
}

/* --- Skin --- */

void skin_init(Skin *s)
{
    memset(s, 0, sizeof(*s));
    skin_colors_init(&s->colors);
}

static void free_bitmaps(Skin *s)
{
    for (int i = 0; i < SKIN_BITMAP_COUNT; i++) {
        if (s->bitmaps[i]) {
            SDL_FreeSurface(s->bitmaps[i]);
            s->bitmaps[i] = NULL;
        }
    }
}

void skin_destroy(Skin *s)
{
    free_bitmaps(s);
    free(s->skin_path);
    s->skin_path = NULL;
}

int skin_bitmap_index(const char *name)
{
    for (int i = 0; i < SKIN_BITMAP_COUNT; i++) {
        /* compare against filename minus ".bmp" */
        const char *file = SKIN_BITMAPS[i];
        size_t stem = strlen(file) - 4;
        if (strncasecmp(name, file, stem) == 0 && name[stem] == '\0')
            return i;
    }
    return -1;
}

SDL_Surface *skin_bitmap(const Skin *s, const char *name)
{
    int idx = skin_bitmap_index(name);
    return idx >= 0 ? s->bitmaps[idx] : NULL;
}

static SDL_Surface *to_argb(SDL_Surface *src)
{
    if (!src)
        return NULL;
    SDL_Surface *conv =
        SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
    SDL_FreeSurface(src);
    return conv;
}

/* Decode image data (BMP or PNG) from memory / disk via SDL2_image. */
static SDL_Surface *load_bmp_mem(const uint8_t *data, size_t len)
{
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)len);
    if (!rw)
        return NULL;
    return to_argb(IMG_Load_RW(rw, 1));
}

static SDL_Surface *load_bmp_file(const char *path)
{
    return to_argb(IMG_Load(path));
}

/* Case-insensitive lookup helper for the two loaders. */
typedef struct {
    char **names_lower; /* lowercased member/file names */
    int count;
} NameIndex;

static int name_index_find(const NameIndex *ni, const char *wanted_lower)
{
    for (int i = 0; i < ni->count; i++)
        if (strcmp(ni->names_lower[i], wanted_lower) == 0)
            return i;
    return -1;
}

static void name_index_free(NameIndex *ni)
{
    for (int i = 0; i < ni->count; i++)
        free(ni->names_lower[i]);
    free(ni->names_lower);
}

/* Try "foo.bmp" then "foo.png" against the index; returns index or -1. */
static int find_sheet(const NameIndex *ni, const char *bmp_name)
{
    char *want = ka_lower(ka_strdup(bmp_name));
    int idx = name_index_find(ni, want);
    if (idx < 0) {
        size_t n = strlen(want);
        memcpy(want + n - 3, "png", 3);
        idx = name_index_find(ni, want);
    }
    free(want);
    return idx;
}

static bool load_directory(Skin *s, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return false;

    NameIndex ni = {0};
    char **real_names = NULL;
    int cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.')
            continue;
        if (ni.count == cap) {
            cap = cap ? cap * 2 : 32;
            ni.names_lower = realloc(ni.names_lower, (size_t)cap * sizeof(char *));
            real_names = realloc(real_names, (size_t)cap * sizeof(char *));
            if (!ni.names_lower || !real_names)
                abort();
        }
        ni.names_lower[ni.count] = ka_lower(ka_strdup(de->d_name));
        real_names[ni.count] = ka_strdup(de->d_name);
        ni.count++;
    }
    closedir(d);

    int loaded = 0;
    for (int i = 0; i < SKIN_BITMAP_COUNT; i++) {
        int fi = find_sheet(&ni, SKIN_BITMAPS[i]);
        if (fi < 0)
            continue;
        char *path = ka_path_join(dir, real_names[fi]);
        SDL_Surface *surf = load_bmp_file(path);
        free(path);
        if (surf) {
            s->bitmaps[i] = surf;
            loaded++;
        }
    }

    for (int i = 0; i < SKIN_CONFIG_COUNT; i++) {
        int fi = name_index_find(&ni, SKIN_CONFIGS[i]);
        if (fi < 0)
            continue;
        char *path = ka_path_join(dir, real_names[fi]);
        char *text = ka_read_file(path, NULL);
        free(path);
        if (!text)
            continue;
        if (strcmp(SKIN_CONFIGS[i], "viscolor.txt") == 0)
            skin_colors_parse_viscolor(&s->colors, text);
        else
            skin_colors_parse_pledit(&s->colors, text);
        free(text);
    }

    for (int i = 0; i < ni.count; i++)
        free(real_names[i]);
    free(real_names);
    name_index_free(&ni);
    return loaded > 0;
}

static bool load_archive(Skin *s, const char *path)
{
    ZipArchive *za = zip_open(path);
    if (!za)
        return false;

    /* Enforce member-count and total-uncompressed-size caps. */
    if (zip_count(za) > SKIN_MAX_MEMBERS) {
        zip_close(za);
        return false;
    }
    uint64_t total = 0;
    for (int i = 0; i < zip_count(za); i++) {
        total += zip_uncompressed_size(za, i);
        if (total > SKIN_MAX_UNCOMPRESSED_BYTES) {
            zip_close(za);
            return false;
        }
    }

    /* Index safe members by lowercased basename (skins may nest files in a
     * single folder inside the archive). */
    NameIndex ni = {0};
    int *member = calloc((size_t)KA_MAX(zip_count(za), 1), sizeof(int));
    if (!member)
        abort();
    ni.names_lower = calloc((size_t)KA_MAX(zip_count(za), 1), sizeof(char *));
    if (!ni.names_lower)
        abort();
    for (int i = 0; i < zip_count(za); i++) {
        if (zip_is_symlink(za, i) || zip_is_unsafe_name(za, i))
            continue;
        const char *base = ka_basename(zip_name(za, i));
        if (!*base)
            continue; /* directory entry */
        member[ni.count] = i;
        ni.names_lower[ni.count] = ka_lower(ka_strdup(base));
        ni.count++;
    }

    int loaded = 0;
    for (int i = 0; i < SKIN_BITMAP_COUNT; i++) {
        int fi = find_sheet(&ni, SKIN_BITMAPS[i]);
        if (fi < 0)
            continue;
        size_t len;
        uint8_t *data = zip_read(za, member[fi], &len);
        if (!data)
            continue;
        SDL_Surface *surf = load_bmp_mem(data, len);
        free(data);
        if (surf) {
            s->bitmaps[i] = surf;
            loaded++;
        }
    }

    for (int i = 0; i < SKIN_CONFIG_COUNT; i++) {
        int fi = name_index_find(&ni, SKIN_CONFIGS[i]);
        if (fi < 0)
            continue;
        size_t len;
        uint8_t *text = zip_read(za, member[fi], &len);
        if (!text)
            continue;
        if (strcmp(SKIN_CONFIGS[i], "viscolor.txt") == 0)
            skin_colors_parse_viscolor(&s->colors, (const char *)text);
        else
            skin_colors_parse_pledit(&s->colors, (const char *)text);
        free(text);
    }

    free(member);
    name_index_free(&ni);
    zip_close(za);
    return loaded > 0;
}

bool skin_load(Skin *s, const char *path)
{
    free_bitmaps(s);
    skin_colors_init(&s->colors);
    free(s->skin_path);
    s->skin_path = ka_strdup(path);

    if (ka_is_dir(path))
        return load_directory(s, path);

    if (ka_is_file(path)) {
        char *ext = ka_ext_lower(path);
        bool is_archive =
            strcmp(ext, ".wsz") == 0 || strcmp(ext, ".zip") == 0;
        free(ext);
        if (is_archive)
            return load_archive(s, path);
    }
    return false;
}
