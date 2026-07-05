/* Playlist model with M3U/PLS support, shuffle, repeat, and sorting.
 * Port of nixamp lib/playlist.py. */
#ifndef KA_PLAYLIST_H
#define KA_PLAYLIST_H

#include "common.h"

enum {
    REPEAT_OFF = 0,
    REPEAT_ALL = 1,
    REPEAT_SINGLE = 2,
};

typedef struct {
    char *filepath;
    char *title;    /* file stem when no tag/EXTINF title known */
    char *artist;   /* "" if unknown */
    char *album;    /* "" if unknown */
    int duration;   /* seconds, -1 = unknown */
    int bitrate;    /* kbps */
    int sample_rate;
    int channels;
} Track;

/* "Artist - Title" or just "Title"; heap string, caller frees. */
char *track_display_title(const Track *t);
const char *track_filename(const Track *t);
/* "M:SS" or "" when duration unknown; heap string, caller frees. */
char *track_duration_str(const Track *t);

typedef struct Playlist Playlist;

/* Change callbacks (both optional). */
typedef void (*PlaylistChangedFn)(void *ud);
typedef void (*PlaylistCurrentFn)(void *ud, int index);

Playlist *playlist_new(void);
void playlist_free(Playlist *pl);
void playlist_set_callbacks(Playlist *pl, PlaylistChangedFn changed,
                            PlaylistCurrentFn current, void *ud);

int playlist_count(const Playlist *pl);
Track *playlist_track(const Playlist *pl, int index); /* NULL if out of range */
int playlist_current_index(const Playlist *pl);
Track *playlist_current_track(const Playlist *pl);
/* Sets current by index; returns the track or NULL if out of range. */
Track *playlist_set_current(Playlist *pl, int index);

bool playlist_shuffle(const Playlist *pl);
void playlist_set_shuffle(Playlist *pl, bool on);
void playlist_toggle_shuffle(Playlist *pl);
int playlist_repeat(const Playlist *pl);
void playlist_set_repeat(Playlist *pl, int mode);
void playlist_toggle_repeat(Playlist *pl); /* cycles OFF -> ALL -> SINGLE */

int playlist_total_duration(const Playlist *pl); /* seconds, known tracks only */
/* "H:MM:SS" or "M:SS"; heap string, caller frees. */
char *playlist_total_duration_str(const Playlist *pl);

void playlist_add_file(Playlist *pl, const char *filepath);
void playlist_add_files(Playlist *pl, const char **files, int n);
void playlist_add_directory(Playlist *pl, const char *dirpath, bool recursive);
void playlist_remove(Playlist *pl, const int *indices, int n);
void playlist_clear(Playlist *pl);
void playlist_move(Playlist *pl, int from_idx, int to_idx);

/* Navigation respecting shuffle/repeat; NULL means "stay stopped". */
Track *playlist_next_track(Playlist *pl);
Track *playlist_prev_track(Playlist *pl);

void playlist_sort_by_filename(Playlist *pl);
void playlist_sort_by_title(Playlist *pl);
void playlist_sort_by_duration(Playlist *pl);
void playlist_sort_by_path(Playlist *pl);
void playlist_reverse(Playlist *pl);

void playlist_load_m3u(Playlist *pl, const char *filepath);
void playlist_save_m3u(Playlist *pl, const char *filepath);
void playlist_load_pls(Playlist *pl, const char *filepath);
void playlist_save_pls(Playlist *pl, const char *filepath);

/* True if extension is a supported audio type (used by add_* and loaders). */
bool playlist_is_audio_ext(const char *path);

#endif
