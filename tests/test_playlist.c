/* Playlist model tests: add/remove/nav/shuffle/repeat/sort/M3U/PLS.
 * Ports tests/test_playlist.py and test_playlist_nav.py. */
#include "ktest.h"

#include <unistd.h>

#include "playlist.h"

static char *g_dir;

static char *make_audio_file(const char *name)
{
    char *path = ka_path_join(g_dir, name);
    FILE *f = fopen(path, "w");
    fputs("x", f);
    fclose(f);
    return path;
}

static Playlist *filled(char *paths[3])
{
    Playlist *pl = playlist_new();
    paths[0] = make_audio_file("aaa.mp3");
    paths[1] = make_audio_file("bbb.ogg");
    paths[2] = make_audio_file("ccc.flac");
    for (int i = 0; i < 3; i++)
        playlist_add_file(pl, paths[i]);
    return pl;
}

static void test_add_file(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    ASSERT_EQ_INT(playlist_count(pl), 3);
    ASSERT_STR_EQ(playlist_track(pl, 0)->title, "aaa");
    /* Non-audio and missing files are rejected */
    char *txt = make_audio_file("readme.txt");
    playlist_add_file(pl, txt);
    ASSERT_EQ_INT(playlist_count(pl), 3);
    playlist_add_file(pl, "/nonexistent/foo.mp3");
    ASSERT_EQ_INT(playlist_count(pl), 3);
    free(txt);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_remove(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_set_current(pl, 2);
    int idx[] = {0};
    playlist_remove(pl, idx, 1);
    ASSERT_EQ_INT(playlist_count(pl), 2);
    ASSERT_EQ_INT(playlist_current_index(pl), 1); /* shifted down */
    playlist_clear(pl);
    ASSERT_EQ_INT(playlist_count(pl), 0);
    ASSERT_EQ_INT(playlist_current_index(pl), -1);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_navigation(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_set_current(pl, 0);
    Track *t = playlist_next_track(pl);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ_INT(playlist_current_index(pl), 1);
    playlist_next_track(pl);
    /* At end without repeat: next returns NULL, index stays at last */
    ASSERT_TRUE(playlist_next_track(pl) == NULL);
    ASSERT_EQ_INT(playlist_current_index(pl), 2);
    /* prev walks back; at 0 returns current track */
    playlist_prev_track(pl);
    ASSERT_EQ_INT(playlist_current_index(pl), 1);
    playlist_prev_track(pl);
    ASSERT_EQ_INT(playlist_current_index(pl), 0);
    ASSERT_TRUE(playlist_prev_track(pl) != NULL);
    ASSERT_EQ_INT(playlist_current_index(pl), 0);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_repeat_all(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_set_repeat(pl, REPEAT_ALL);
    playlist_set_current(pl, 2);
    Track *t = playlist_next_track(pl);
    ASSERT_TRUE(t != NULL);
    ASSERT_EQ_INT(playlist_current_index(pl), 0); /* wrapped */
    /* REPEAT_SINGLE returns the same track */
    playlist_set_repeat(pl, REPEAT_SINGLE);
    ASSERT_TRUE(playlist_next_track(pl) == playlist_current_track(pl));
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_shuffle(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_set_current(pl, 1);
    playlist_set_shuffle(pl, true);
    ASSERT_TRUE(playlist_shuffle(pl));
    /* Walk the full shuffle order without repeats or crashes */
    bool seen[3] = {false};
    seen[playlist_current_index(pl)] = true;
    Track *t;
    while ((t = playlist_next_track(pl)) != NULL) {
        int i = playlist_current_index(pl);
        ASSERT_FALSE(seen[i] && "revisited track before order exhausted");
        seen[i] = true;
    }
    ASSERT_TRUE(seen[0] && seen[1] && seen[2]);
    /* set_current realigns shuffle pos (no crash on subsequent nav) */
    playlist_set_current(pl, 0);
    playlist_next_track(pl);
    playlist_prev_track(pl);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_move(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_set_current(pl, 0);
    playlist_move(pl, 0, 2);
    ASSERT_STR_EQ(playlist_track(pl, 2)->title, "aaa");
    ASSERT_EQ_INT(playlist_current_index(pl), 2); /* follows the track */
    playlist_move(pl, 0, 1);
    ASSERT_STR_EQ(playlist_track(pl, 1)->title, "bbb");
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_sort_by_filename(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_reverse(pl);
    ASSERT_STR_EQ(playlist_track(pl, 0)->title, "ccc");
    playlist_sort_by_filename(pl);
    ASSERT_STR_EQ(playlist_track(pl, 0)->title, "aaa");
    ASSERT_STR_EQ(playlist_track(pl, 2)->title, "ccc");
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_display_title_and_duration(void)
{
    char *p = make_audio_file("song.mp3");
    Playlist *pl = playlist_new();
    playlist_add_file(pl, p);
    Track *t = playlist_track(pl, 0);
    char *disp = track_display_title(t);
    ASSERT_STR_EQ(disp, "song");
    free(disp);
    free(t->artist);
    t->artist = ka_strdup("Artist");
    free(t->title);
    t->title = ka_strdup("Title");
    disp = track_display_title(t);
    ASSERT_STR_EQ(disp, "Artist - Title");
    free(disp);

    t->duration = 125;
    char *dur = track_duration_str(t);
    ASSERT_STR_EQ(dur, "2:05");
    free(dur);
    t->duration = -1;
    dur = track_duration_str(t);
    ASSERT_STR_EQ(dur, "");
    free(dur);
    free(p);
    playlist_free(pl);
}

static void test_total_duration(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_track(pl, 0)->duration = 60;
    playlist_track(pl, 1)->duration = 90;
    playlist_track(pl, 2)->duration = -1; /* unknown ignored */
    ASSERT_EQ_INT(playlist_total_duration(pl), 150);
    char *s = playlist_total_duration_str(pl);
    ASSERT_STR_EQ(s, "2:30");
    free(s);
    playlist_track(pl, 2)->duration = 3600;
    s = playlist_total_duration_str(pl);
    ASSERT_STR_EQ(s, "1:02:30");
    free(s);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
    playlist_free(pl);
}

static void test_m3u_save_load(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    Track *t = playlist_track(pl, 0);
    free(t->artist);
    t->artist = ka_strdup("Someone");
    t->duration = 42;
    char *m3u = ka_path_join(g_dir, "list.m3u");
    playlist_save_m3u(pl, m3u);

    Playlist *pl2 = playlist_new();
    playlist_load_m3u(pl2, m3u);
    ASSERT_EQ_INT(playlist_count(pl2), 3);
    ASSERT_STR_EQ(playlist_track(pl2, 0)->artist, "Someone");
    ASSERT_STR_EQ(playlist_track(pl2, 0)->title, "aaa");
    ASSERT_EQ_INT(playlist_track(pl2, 0)->duration, 42);
    playlist_free(pl2);
    playlist_free(pl);
    free(m3u);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
}

static void test_m3u_extinf_fractional_duration(void)
{
    char *p = make_audio_file("frac.mp3");
    char *m3u = ka_path_join(g_dir, "frac.m3u");
    FILE *f = fopen(m3u, "w");
    fprintf(f, "#EXTM3U\n#EXTINF:12.7,Frac Song\n%s\n", p);
    fclose(f);

    Playlist *pl = playlist_new();
    playlist_load_m3u(pl, m3u);
    ASSERT_EQ_INT(playlist_count(pl), 1);
    ASSERT_EQ_INT(playlist_track(pl, 0)->duration, 12);
    ASSERT_STR_EQ(playlist_track(pl, 0)->title, "Frac Song");
    playlist_free(pl);
    free(m3u);
    free(p);
}

static void test_pls_save_load(void)
{
    char *paths[3];
    Playlist *pl = filled(paths);
    playlist_track(pl, 1)->duration = 99;
    char *pls = ka_path_join(g_dir, "list.pls");
    playlist_save_pls(pl, pls);

    Playlist *pl2 = playlist_new();
    playlist_load_pls(pl2, pls);
    ASSERT_EQ_INT(playlist_count(pl2), 3);
    ASSERT_EQ_INT(playlist_track(pl2, 1)->duration, 99);
    playlist_free(pl2);
    playlist_free(pl);
    free(pls);
    for (int i = 0; i < 3; i++)
        free(paths[i]);
}

static void test_pls_with_malformed_line_loads_valid(void)
{
    char *p = make_audio_file("ok.mp3");
    char *pls = ka_path_join(g_dir, "mal.pls");
    FILE *f = fopen(pls, "w");
    fprintf(f,
            "[playlist]\n"
            "garbage line without equals\n"
            "FileX=broken\n"
            "File1=%s\n"
            "Title1=Good One\n"
            "LengthQ=notanumber\n"
            "NumberOfEntries=1\n",
            p);
    fclose(f);

    Playlist *pl = playlist_new();
    playlist_load_pls(pl, pls);
    ASSERT_EQ_INT(playlist_count(pl), 1);
    ASSERT_STR_EQ(playlist_track(pl, 0)->title, "Good One");
    playlist_free(pl);
    free(pls);
    free(p);
}

static void test_is_audio_ext(void)
{
    ASSERT_TRUE(playlist_is_audio_ext("/x/y.mp3"));
    ASSERT_TRUE(playlist_is_audio_ext("/x/y.FLAC"));
    ASSERT_TRUE(playlist_is_audio_ext("y.opus"));
    ASSERT_TRUE(playlist_is_audio_ext("song.MID"));
    ASSERT_TRUE(playlist_is_audio_ext("song.midi"));
    ASSERT_FALSE(playlist_is_audio_ext("y.txt"));
    ASSERT_FALSE(playlist_is_audio_ext("y"));
}

int main(void)
{
    g_dir = kt_tmpdir();
    RUN(test_add_file);
    RUN(test_remove);
    RUN(test_navigation);
    RUN(test_repeat_all);
    RUN(test_shuffle);
    RUN(test_move);
    RUN(test_sort_by_filename);
    RUN(test_display_title_and_duration);
    RUN(test_total_duration);
    RUN(test_m3u_save_load);
    RUN(test_m3u_extinf_fractional_duration);
    RUN(test_pls_save_load);
    RUN(test_pls_with_malformed_line_loads_valid);
    RUN(test_is_audio_ext);
    return kt_summary("test_playlist");
}
