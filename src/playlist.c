#include "playlist.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *AUDIO_EXTENSIONS[] = {
    ".mp3", ".mp2", ".mp1", ".ogg", ".oga", ".opus", ".flac", ".wav",
    ".aac", ".m4a", ".wma", ".ape", ".mpc", ".wv",   ".aiff", ".aif",
    ".mid", ".midi", ".mod", ".s3m", ".xm",  ".it",
};

bool playlist_is_audio_ext(const char *path)
{
    char *ext = ka_ext_lower(path);
    bool ok = false;
    for (size_t i = 0; i < KA_LEN(AUDIO_EXTENSIONS) && !ok; i++)
        ok = strcmp(ext, AUDIO_EXTENSIONS[i]) == 0;
    free(ext);
    return ok;
}

/* --- Track --- */

static Track track_make(const char *filepath, const char *title, int duration)
{
    Track t = {0};
    t.filepath = ka_strdup(filepath);
    t.title = (title && *title) ? ka_strdup(title) : ka_stem(filepath);
    t.artist = ka_strdup("");
    t.album = ka_strdup("");
    t.duration = duration;
    return t;
}

static void track_destroy(Track *t)
{
    free(t->filepath);
    free(t->title);
    free(t->artist);
    free(t->album);
}

char *track_display_title(const Track *t)
{
    if (t->artist && *t->artist)
        return ka_asprintf("%s - %s", t->artist, t->title);
    return ka_strdup(t->title);
}

const char *track_filename(const Track *t)
{
    return ka_basename(t->filepath);
}

char *track_duration_str(const Track *t)
{
    if (t->duration < 0)
        return ka_strdup("");
    return ka_asprintf("%d:%02d", t->duration / 60, t->duration % 60);
}

/* --- Playlist --- */

struct Playlist {
    Track *tracks;
    int count, cap;
    int current_index;
    bool shuffle;
    int repeat;
    int *shuffle_order;
    int shuffle_pos;

    PlaylistChangedFn on_changed;
    PlaylistCurrentFn on_current;
    void *ud;
};

static void emit_changed(Playlist *pl)
{
    if (pl->on_changed)
        pl->on_changed(pl->ud);
}

static void emit_current(Playlist *pl)
{
    if (pl->on_current)
        pl->on_current(pl->ud, pl->current_index);
}

Playlist *playlist_new(void)
{
    Playlist *pl = calloc(1, sizeof(*pl));
    if (!pl)
        abort();
    pl->current_index = -1;
    return pl;
}

void playlist_free(Playlist *pl)
{
    if (!pl)
        return;
    for (int i = 0; i < pl->count; i++)
        track_destroy(&pl->tracks[i]);
    free(pl->tracks);
    free(pl->shuffle_order);
    free(pl);
}

void playlist_set_callbacks(Playlist *pl, PlaylistChangedFn changed,
                            PlaylistCurrentFn current, void *ud)
{
    pl->on_changed = changed;
    pl->on_current = current;
    pl->ud = ud;
}

int playlist_count(const Playlist *pl) { return pl->count; }

Track *playlist_track(const Playlist *pl, int index)
{
    if (index < 0 || index >= pl->count)
        return NULL;
    return &pl->tracks[index];
}

int playlist_current_index(const Playlist *pl) { return pl->current_index; }

Track *playlist_current_track(const Playlist *pl)
{
    return playlist_track(pl, pl->current_index);
}

static void generate_shuffle_order(Playlist *pl)
{
    free(pl->shuffle_order);
    pl->shuffle_order = malloc(sizeof(int) * (size_t)KA_MAX(pl->count, 1));
    if (!pl->shuffle_order)
        abort();
    for (int i = 0; i < pl->count; i++)
        pl->shuffle_order[i] = i;
    /* Fisher-Yates */
    for (int i = pl->count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int tmp = pl->shuffle_order[i];
        pl->shuffle_order[i] = pl->shuffle_order[j];
        pl->shuffle_order[j] = tmp;
    }
    /* Put current track at position 0 if set.  With no current track, start
     * before the order so the first Next selects shuffle_order[0]. */
    pl->shuffle_pos = -1;
    if (pl->current_index >= 0 && pl->current_index < pl->count) {
        for (int i = 0; i < pl->count; i++) {
            if (pl->shuffle_order[i] == pl->current_index) {
                int tmp = pl->shuffle_order[0];
                pl->shuffle_order[0] = pl->shuffle_order[i];
                pl->shuffle_order[i] = tmp;
                break;
            }
        }
        pl->shuffle_pos = 0;
    }
}

static void sync_shuffle_pos(Playlist *pl, int index)
{
    if (!pl->shuffle || !pl->shuffle_order)
        return;
    for (int i = 0; i < pl->count; i++) {
        if (pl->shuffle_order[i] == index) {
            pl->shuffle_pos = i;
            return;
        }
    }
}

Track *playlist_set_current(Playlist *pl, int index)
{
    if (index < 0 || index >= pl->count)
        return NULL;
    pl->current_index = index;
    sync_shuffle_pos(pl, index);
    emit_current(pl);
    return &pl->tracks[index];
}

bool playlist_shuffle(const Playlist *pl) { return pl->shuffle; }

void playlist_set_shuffle(Playlist *pl, bool on)
{
    pl->shuffle = on;
    if (on)
        generate_shuffle_order(pl);
}

void playlist_toggle_shuffle(Playlist *pl)
{
    playlist_set_shuffle(pl, !pl->shuffle);
}

int playlist_repeat(const Playlist *pl) { return pl->repeat; }
void playlist_set_repeat(Playlist *pl, int mode) { pl->repeat = mode; }

void playlist_toggle_repeat(Playlist *pl)
{
    pl->repeat = (pl->repeat + 1) % 3;
}

int playlist_total_duration(const Playlist *pl)
{
    int total = 0;
    for (int i = 0; i < pl->count; i++)
        if (pl->tracks[i].duration > 0)
            total += pl->tracks[i].duration;
    return total;
}

char *playlist_total_duration_str(const Playlist *pl)
{
    int total = playlist_total_duration(pl);
    int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return ka_asprintf("%d:%02d:%02d", h, m, s);
    return ka_asprintf("%d:%02d", m, s);
}

/* --- Track management --- */

static void push_track(Playlist *pl, Track t)
{
    if (pl->count == pl->cap) {
        pl->cap = pl->cap ? pl->cap * 2 : 64;
        pl->tracks = realloc(pl->tracks, (size_t)pl->cap * sizeof(Track));
        if (!pl->tracks)
            abort();
    }
    pl->tracks[pl->count++] = t;
}

void playlist_add_file(Playlist *pl, const char *filepath)
{
    if (!ka_is_file(filepath) || !playlist_is_audio_ext(filepath))
        return;
    push_track(pl, track_make(filepath, NULL, -1));
    if (pl->shuffle)
        generate_shuffle_order(pl);
    emit_changed(pl);
}

void playlist_add_files(Playlist *pl, const char **files, int n)
{
    for (int i = 0; i < n; i++)
        if (playlist_is_audio_ext(files[i]) && ka_is_file(files[i]))
            push_track(pl, track_make(files[i], NULL, -1));
    if (pl->shuffle)
        generate_shuffle_order(pl);
    emit_changed(pl);
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Collect sorted entries of a directory: names go to *out (heap array of
 * heap strings), returns count. */
static int list_dir_sorted(const char *dirpath, char ***out)
{
    DIR *d = opendir(dirpath);
    *out = NULL;
    if (!d)
        return 0;
    char **names = NULL;
    int n = 0, cap = 0;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            names = realloc(names, (size_t)cap * sizeof(char *));
            if (!names)
                abort();
        }
        names[n++] = ka_strdup(de->d_name);
    }
    closedir(d);
    if (names)
        qsort(names, (size_t)n, sizeof(char *), cmp_str);
    *out = names;
    return n;
}

/* Do not follow directory symlinks during recursive imports. Besides avoiding
 * surprising traversal outside the selected tree, this prevents parent/self
 * symlinks from creating cycles. The explicitly selected root may still be a
 * symlink; only descendants are filtered here. */
static bool is_real_dir(const char *path)
{
    struct stat st;
    return lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void add_dir_recursive(Playlist *pl, const char *dirpath)
{
    char **names;
    int n = list_dir_sorted(dirpath, &names);
    /* Files first pass sorted; walk dirs sorted too (os.walk visits files of
     * the current dir, then recurses into each subdir in sorted order). */
    for (int i = 0; i < n; i++) {
        char *full = ka_path_join(dirpath, names[i]);
        if (ka_is_file(full) && playlist_is_audio_ext(full))
            push_track(pl, track_make(full, NULL, -1));
        free(full);
    }
    for (int i = 0; i < n; i++) {
        char *full = ka_path_join(dirpath, names[i]);
        if (is_real_dir(full))
            add_dir_recursive(pl, full);
        free(full);
        free(names[i]);
    }
    free(names);
}

void playlist_add_directory(Playlist *pl, const char *dirpath, bool recursive)
{
    if (recursive) {
        add_dir_recursive(pl, dirpath);
    } else {
        char **names;
        int n = list_dir_sorted(dirpath, &names);
        for (int i = 0; i < n; i++) {
            char *full = ka_path_join(dirpath, names[i]);
            if (ka_is_file(full) && playlist_is_audio_ext(full))
                push_track(pl, track_make(full, NULL, -1));
            free(full);
            free(names[i]);
        }
        free(names);
    }
    if (pl->shuffle)
        generate_shuffle_order(pl);
    emit_changed(pl);
}

static int cmp_int_desc(const void *a, const void *b)
{
    return *(const int *)b - *(const int *)a;
}

void playlist_remove(Playlist *pl, const int *indices, int n)
{
    int *sorted = malloc(sizeof(int) * (size_t)KA_MAX(n, 1));
    if (!sorted)
        abort();
    memcpy(sorted, indices, sizeof(int) * (size_t)n);
    qsort(sorted, (size_t)n, sizeof(int), cmp_int_desc);
    for (int k = 0; k < n; k++) {
        int idx = sorted[k];
        if (idx < 0 || idx >= pl->count)
            continue;
        track_destroy(&pl->tracks[idx]);
        memmove(&pl->tracks[idx], &pl->tracks[idx + 1],
                (size_t)(pl->count - idx - 1) * sizeof(Track));
        pl->count--;
        if (pl->current_index == idx)
            pl->current_index = KA_MIN(idx, pl->count - 1);
        else if (pl->current_index > idx)
            pl->current_index--;
    }
    free(sorted);
    if (pl->shuffle)
        generate_shuffle_order(pl);
    emit_changed(pl);
}

void playlist_clear(Playlist *pl)
{
    for (int i = 0; i < pl->count; i++)
        track_destroy(&pl->tracks[i]);
    pl->count = 0;
    pl->current_index = -1;
    free(pl->shuffle_order);
    pl->shuffle_order = NULL;
    pl->shuffle_pos = 0;
    emit_changed(pl);
}

void playlist_move(Playlist *pl, int from_idx, int to_idx)
{
    if (from_idx < 0 || from_idx >= pl->count)
        return;
    to_idx = KA_CLAMP(to_idx, 0, pl->count - 1);
    Track t = pl->tracks[from_idx];
    if (from_idx < to_idx)
        memmove(&pl->tracks[from_idx], &pl->tracks[from_idx + 1],
                (size_t)(to_idx - from_idx) * sizeof(Track));
    else
        memmove(&pl->tracks[to_idx + 1], &pl->tracks[to_idx],
                (size_t)(from_idx - to_idx) * sizeof(Track));
    pl->tracks[to_idx] = t;
    if (pl->current_index == from_idx)
        pl->current_index = to_idx;
    else if (from_idx < pl->current_index && pl->current_index <= to_idx)
        pl->current_index--;
    else if (to_idx <= pl->current_index && pl->current_index < from_idx)
        pl->current_index++;
    emit_changed(pl);
}

/* --- Navigation --- */

Track *playlist_next_track(Playlist *pl)
{
    if (pl->count == 0)
        return NULL;

    if (pl->repeat == REPEAT_SINGLE)
        return playlist_current_track(pl);

    if (pl->shuffle) {
        if (!pl->shuffle_order)
            generate_shuffle_order(pl);
        pl->shuffle_pos++;
        if (pl->shuffle_pos >= pl->count) {
            if (pl->repeat == REPEAT_ALL) {
                generate_shuffle_order(pl);
                pl->shuffle_pos = 0;
            } else {
                pl->shuffle_pos = pl->count - 1;
                return NULL;
            }
        }
        pl->current_index = pl->shuffle_order[pl->shuffle_pos];
    } else {
        pl->current_index++;
        if (pl->current_index >= pl->count) {
            if (pl->repeat == REPEAT_ALL) {
                pl->current_index = 0;
            } else {
                pl->current_index = pl->count - 1;
                return NULL;
            }
        }
    }
    emit_current(pl);
    return playlist_current_track(pl);
}

Track *playlist_prev_track(Playlist *pl)
{
    if (pl->count == 0)
        return NULL;

    if (pl->shuffle) {
        if (!pl->shuffle_order)
            generate_shuffle_order(pl);
        pl->shuffle_pos--;
        if (pl->shuffle_pos < 0) {
            if (pl->repeat == REPEAT_ALL) {
                pl->shuffle_pos = pl->count - 1;
            } else {
                pl->shuffle_pos = 0;
                return playlist_current_track(pl);
            }
        }
        pl->current_index = pl->shuffle_order[pl->shuffle_pos];
    } else {
        pl->current_index--;
        if (pl->current_index < 0) {
            if (pl->repeat == REPEAT_ALL)
                pl->current_index = pl->count - 1;
            else
                pl->current_index = 0;
        }
    }
    emit_current(pl);
    return playlist_current_track(pl);
}

/* --- Sorting --- */

static int cmp_by_filename(const void *a, const void *b)
{
    return strcasecmp(track_filename(a), track_filename(b));
}

static int cmp_by_title(const void *a, const void *b)
{
    char *ta = track_display_title(a);
    char *tb = track_display_title(b);
    int r = strcasecmp(ta, tb);
    free(ta);
    free(tb);
    return r;
}

static int cmp_by_duration(const void *a, const void *b)
{
    return ((const Track *)a)->duration - ((const Track *)b)->duration;
}

static int cmp_by_path(const void *a, const void *b)
{
    return strcasecmp(((const Track *)a)->filepath,
                      ((const Track *)b)->filepath);
}

/* Like the original, the numeric current_index is left as-is after a sort. */
static void sort_tracks(Playlist *pl, int (*cmp)(const void *, const void *))
{
    qsort(pl->tracks, (size_t)pl->count, sizeof(Track), cmp);
    emit_changed(pl);
}

void playlist_sort_by_filename(Playlist *pl) { sort_tracks(pl, cmp_by_filename); }
void playlist_sort_by_title(Playlist *pl) { sort_tracks(pl, cmp_by_title); }
void playlist_sort_by_duration(Playlist *pl) { sort_tracks(pl, cmp_by_duration); }
void playlist_sort_by_path(Playlist *pl) { sort_tracks(pl, cmp_by_path); }

void playlist_reverse(Playlist *pl)
{
    for (int i = 0, j = pl->count - 1; i < j; i++, j--) {
        Track t = pl->tracks[i];
        pl->tracks[i] = pl->tracks[j];
        pl->tracks[j] = t;
    }
    emit_changed(pl);
}

/* --- M3U / PLS --- */

static bool is_valid_audio(const char *filepath)
{
    return ka_is_file(filepath) && playlist_is_audio_ext(filepath);
}

/* Restore title/artist from a display string, splitting on first " - ". */
static void apply_display_title(Track *t, const char *display)
{
    const char *sep = strstr(display, " - ");
    if (sep) {
        free(t->artist);
        free(t->title);
        t->artist = ka_strndup(display, (size_t)(sep - display));
        t->title = ka_strdup(sep + 3);
    } else {
        free(t->title);
        t->title = ka_strdup(display);
    }
}

static char *resolve_path(const char *basedir, const char *entry)
{
    if (entry[0] == '/')
        return ka_strdup(entry);
    return ka_path_join(basedir, entry);
}

void playlist_load_m3u(Playlist *pl, const char *filepath)
{
    char *data = ka_read_file(filepath, NULL);
    if (!data)
        return;
    char *basedir = ka_dirname(filepath);

    char display[1024] = "";
    int duration = -1;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *s = ka_strip(line);
        if (!*s || strcmp(s, "#EXTM3U") == 0)
            continue;
        if (strncmp(s, "#EXTINF:", 8) == 0) {
            /* #EXTINF:duration,title */
            char *info = s + 8;
            char *comma = strchr(info, ',');
            char *end;
            double d = strtod(info, &end);
            duration = (end == info) ? -1 : (int)d;
            if (comma)
                snprintf(display, sizeof(display), "%s", comma + 1);
            else
                display[0] = '\0';
        } else if (*s != '#') {
            char *full = resolve_path(basedir, s);
            if (is_valid_audio(full)) {
                Track t = track_make(full, NULL, duration);
                if (display[0])
                    apply_display_title(&t, display);
                push_track(pl, t);
            }
            free(full);
            display[0] = '\0';
            duration = -1;
        }
    }
    free(basedir);
    free(data);
    if (pl->shuffle)
        generate_shuffle_order(pl);
    emit_changed(pl);
}

void playlist_save_m3u(Playlist *pl, const char *filepath)
{
    FILE *f = fopen(filepath, "w");
    if (!f)
        return;
    fputs("#EXTM3U\n", f);
    for (int i = 0; i < pl->count; i++) {
        Track *t = &pl->tracks[i];
        char *disp = track_display_title(t);
        fprintf(f, "#EXTINF:%d,%s\n%s\n", t->duration, disp, t->filepath);
        free(disp);
    }
    fclose(f);
}

typedef struct {
    int num;
    char *file;
    char *title;
    int length;
    bool has_length;
} PLSEntry;

static PLSEntry *pls_entry(PLSEntry **entries, int *n, int *cap, int num)
{
    for (int i = 0; i < *n; i++)
        if ((*entries)[i].num == num)
            return &(*entries)[i];
    if (*n == *cap) {
        *cap = *cap ? *cap * 2 : 16;
        *entries = realloc(*entries, (size_t)*cap * sizeof(PLSEntry));
        if (!*entries)
            abort();
    }
    PLSEntry *e = &(*entries)[(*n)++];
    memset(e, 0, sizeof(*e));
    e->num = num;
    e->length = -1;
    return e;
}

static int cmp_pls(const void *a, const void *b)
{
    return ((const PLSEntry *)a)->num - ((const PLSEntry *)b)->num;
}

void playlist_load_pls(Playlist *pl, const char *filepath)
{
    char *data = ka_read_file(filepath, NULL);
    if (!data)
        return;
    char *basedir = ka_dirname(filepath);

    PLSEntry *entries = NULL;
    int n = 0, cap = 0;

    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line;
         line = strtok_r(NULL, "\n", &save)) {
        char *s = ka_strip(line);
        char *eq = strchr(s, '=');
        if (!eq)
            continue;
        *eq = '\0';
        char *key = ka_lower(ka_strip(s));
        char *val = ka_strip(eq + 1);
        char *end;
        if (strncmp(key, "file", 4) == 0) {
            long num = strtol(key + 4, &end, 10);
            if (end == key + 4 || *end)
                continue;
            PLSEntry *e = pls_entry(&entries, &n, &cap, (int)num);
            free(e->file);
            e->file = ka_strdup(val);
        } else if (strncmp(key, "title", 5) == 0) {
            long num = strtol(key + 5, &end, 10);
            if (end == key + 5 || *end)
                continue;
            PLSEntry *e = pls_entry(&entries, &n, &cap, (int)num);
            free(e->title);
            e->title = ka_strdup(val);
        } else if (strncmp(key, "length", 6) == 0) {
            long num = strtol(key + 6, &end, 10);
            if (end == key + 6 || *end)
                continue;
            PLSEntry *e = pls_entry(&entries, &n, &cap, (int)num);
            char *lend;
            long len = strtol(val, &lend, 10);
            e->length = (lend == val || *lend) ? -1 : (int)len;
        }
    }

    qsort(entries, (size_t)n, sizeof(PLSEntry), cmp_pls);
    for (int i = 0; i < n; i++) {
        PLSEntry *e = &entries[i];
        char *full = resolve_path(basedir, e->file ? e->file : "");
        if (is_valid_audio(full)) {
            Track t = track_make(full, NULL, e->length);
            if (e->title && *e->title)
                apply_display_title(&t, e->title);
            push_track(pl, t);
        }
        free(full);
        free(e->file);
        free(e->title);
    }
    free(entries);
    free(basedir);
    free(data);
    if (pl->shuffle)
        generate_shuffle_order(pl);
    emit_changed(pl);
}

void playlist_save_pls(Playlist *pl, const char *filepath)
{
    FILE *f = fopen(filepath, "w");
    if (!f)
        return;
    fputs("[playlist]\n", f);
    for (int i = 0; i < pl->count; i++) {
        Track *t = &pl->tracks[i];
        char *disp = track_display_title(t);
        fprintf(f, "File%d=%s\nTitle%d=%s\nLength%d=%d\n",
                i + 1, t->filepath, i + 1, disp, i + 1, t->duration);
        free(disp);
    }
    fprintf(f, "NumberOfEntries=%d\nVersion=2\n", pl->count);
    fclose(f);
}
