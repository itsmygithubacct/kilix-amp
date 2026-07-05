/* Skin engine + archive tests: viscolor/pledit parsing, default skin
 * generation + loading, zip-bomb and member-count caps.
 * Ports tests/test_skin_engine.py and test_skin_archive.py. */
#include "ktest.h"

#include <stdint.h>

#include "skin.h"
#include "skin_default.h"
#include "zip.h"

/* --- Minimal ZIP writer (stored entries) for archive tests --- */

typedef struct {
    FILE *f;
    long cd_entries[4096][2]; /* offset of local header, name index */
    char names[4096][64];
    uint32_t usizes[4096];
    uint32_t csizes[4096];
    int count;
} ZipWriter;

static void zw_open(ZipWriter *zw, const char *path)
{
    memset(zw, 0, sizeof(*zw));
    zw->f = fopen(path, "wb");
}

static void w16(FILE *f, uint16_t v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }
static void w32(FILE *f, uint32_t v)
{
    w16(f, (uint16_t)(v & 0xFFFF));
    w16(f, (uint16_t)(v >> 16));
}

/* Add a stored entry; fake_usize overrides the recorded uncompressed size
 * (to fabricate zip bombs without writing the bytes). */
static void zw_add(ZipWriter *zw, const char *name, const void *data,
                   uint32_t len, uint32_t fake_usize)
{
    long off = ftell(zw->f);
    zw->cd_entries[zw->count][0] = off;
    snprintf(zw->names[zw->count], sizeof(zw->names[0]), "%s", name);
    zw->csizes[zw->count] = len;
    zw->usizes[zw->count] = fake_usize ? fake_usize : len;

    FILE *f = zw->f;
    w32(f, 0x04034b50);
    w16(f, 20);   /* version needed */
    w16(f, 0);    /* flags */
    w16(f, 0);    /* method: stored */
    w16(f, 0);    /* time */
    w16(f, 0);    /* date */
    w32(f, 0);    /* crc (unchecked by our reader) */
    w32(f, len);  /* csize */
    w32(f, zw->usizes[zw->count]); /* usize */
    w16(f, (uint16_t)strlen(name));
    w16(f, 0); /* extra len */
    fwrite(name, 1, strlen(name), f);
    fwrite(data, 1, len, f);
    zw->count++;
}

static void zw_close(ZipWriter *zw)
{
    FILE *f = zw->f;
    long cd_off = ftell(f);
    for (int i = 0; i < zw->count; i++) {
        w32(f, 0x02014b50);
        w16(f, 20); /* made by */
        w16(f, 20); /* needed */
        w16(f, 0);  /* flags */
        w16(f, 0);  /* method */
        w16(f, 0);  /* time */
        w16(f, 0);  /* date */
        w32(f, 0);  /* crc */
        w32(f, zw->csizes[i]);
        w32(f, zw->usizes[i]);
        w16(f, (uint16_t)strlen(zw->names[i]));
        w16(f, 0); /* extra */
        w16(f, 0); /* comment */
        w16(f, 0); /* disk */
        w16(f, 0); /* internal attr */
        w32(f, 0); /* external attr */
        w32(f, (uint32_t)zw->cd_entries[i][0]);
        fwrite(zw->names[i], 1, strlen(zw->names[i]), f);
    }
    long cd_end = ftell(f);
    w32(f, 0x06054b50);
    w16(f, 0);
    w16(f, 0);
    w16(f, (uint16_t)zw->count);
    w16(f, (uint16_t)zw->count);
    w32(f, (uint32_t)(cd_end - cd_off));
    w32(f, (uint32_t)cd_off);
    w16(f, 0); /* comment len */
    fclose(f);
}

static char *g_dir;

/* --- Color parsing --- */

static void test_viscolor_parsing(void)
{
    SkinColors sc;
    skin_colors_init(&sc);
    skin_colors_parse_viscolor(&sc,
                               "10,20,30 // background\n"
                               "1, 2, 3\n"
                               "not a color line\n"
                               "255,255,255\n");
    ASSERT_EQ_INT(sc.viscolors[0].r, 10);
    ASSERT_EQ_INT(sc.viscolors[0].g, 20);
    ASSERT_EQ_INT(sc.viscolors[0].b, 30);
    ASSERT_EQ_INT(sc.viscolors[1].r, 1);
    ASSERT_EQ_INT(sc.viscolors[2].r, 255);
    /* Remaining entries keep defaults */
    ASSERT_EQ_INT(sc.viscolors[3].r, DEFAULT_VISCOLORS[3].r);
}

static void test_pledit_parsing(void)
{
    SkinColors sc;
    skin_colors_init(&sc);
    skin_colors_parse_pledit(&sc,
                             "[Text]\n"
                             "Normal=#FF0000\n"
                             "current=00FF00\n" /* no #, gets prepended */
                             "NormalBG=#012345\n"
                             "SelectedBG=#ABC\n" /* 3-digit form */
                             "Font=Helvetica\n"
                             "bogus line\n");
    ASSERT_EQ_INT(sc.pl_normal.r, 0xFF);
    ASSERT_EQ_INT(sc.pl_normal.g, 0);
    ASSERT_EQ_INT(sc.pl_current.g, 0xFF);
    ASSERT_EQ_INT(sc.pl_normalbg.r, 0x01);
    ASSERT_EQ_INT(sc.pl_normalbg.g, 0x23);
    ASSERT_EQ_INT(sc.pl_normalbg.b, 0x45);
    ASSERT_EQ_INT(sc.pl_selectedbg.r, 0xAA);
    ASSERT_EQ_INT(sc.pl_selectedbg.g, 0xBB);
    ASSERT_EQ_INT(sc.pl_selectedbg.b, 0xCC);
    ASSERT_STR_EQ(sc.pl_font, "Helvetica");
}

/* --- Default skin generation + directory loading --- */

static void test_generate_and_load_default_skin(void)
{
    char *dir = ka_path_join(g_dir, "defskin");
    ASSERT_TRUE(skin_default_generate(dir));

    Skin skin;
    skin_init(&skin);
    ASSERT_TRUE(skin_load(&skin, dir));
    /* All 14 sheets should load */
    for (int i = 0; i < SKIN_BITMAP_COUNT; i++)
        ASSERT_TRUE(skin.bitmaps[i] != NULL);
    /* Sheet dimensions */
    ASSERT_EQ_INT(skin_bitmap(&skin, "main")->w, 275);
    ASSERT_EQ_INT(skin_bitmap(&skin, "main")->h, 116);
    ASSERT_EQ_INT(skin_bitmap(&skin, "text")->w, 31 * 5);
    ASSERT_EQ_INT(skin_bitmap(&skin, "nums_ex")->w, 12 * 9);
    /* viscolor.txt roundtrip */
    ASSERT_EQ_INT(skin.colors.viscolors[2].r, DEFAULT_VISCOLORS[2].r);
    skin_destroy(&skin);
    free(dir);
}

static void test_load_missing_dir_fails(void)
{
    Skin skin;
    skin_init(&skin);
    ASSERT_FALSE(skin_load(&skin, "/nonexistent/skin/dir"));
    skin_destroy(&skin);
}

/* --- Archive handling --- */

static void test_wsz_loads(void)
{
    /* Build a .wsz containing one valid BMP (reuse generated default) and
     * a viscolor.txt */
    char *dir = ka_path_join(g_dir, "defskin");
    size_t len;
    char *bmp_path = ka_path_join(dir, "main.bmp");
    char *bmp = ka_read_file(bmp_path, &len);
    ASSERT_TRUE(bmp != NULL);

    char *wsz = ka_path_join(g_dir, "skin.wsz");
    ZipWriter zw;
    zw_open(&zw, wsz);
    zw_add(&zw, "main.bmp", bmp, (uint32_t)len, 0);
    const char *vis = "9,8,7\n";
    zw_add(&zw, "viscolor.txt", vis, (uint32_t)strlen(vis), 0);
    /* Unsafe + symlink-ish members are skipped, not fatal */
    zw_add(&zw, "../evil.bmp", "x", 1, 0);
    zw_close(&zw);

    Skin skin;
    skin_init(&skin);
    ASSERT_TRUE(skin_load(&skin, wsz));
    ASSERT_TRUE(skin_bitmap(&skin, "main") != NULL);
    ASSERT_EQ_INT(skin.colors.viscolors[0].r, 9);
    skin_destroy(&skin);
    free(bmp);
    free(bmp_path);
    free(wsz);
    free(dir);
}

static void test_zip_bomb_rejected(void)
{
    char *wsz = ka_path_join(g_dir, "bomb.wsz");
    ZipWriter zw;
    zw_open(&zw, wsz);
    /* Two members claiming 150MB uncompressed each: total exceeds the
     * 200MB cap. */
    zw_add(&zw, "a.bmp", "x", 1, 150u * 1024 * 1024);
    zw_add(&zw, "b.bmp", "x", 1, 150u * 1024 * 1024);
    zw_close(&zw);

    Skin skin;
    skin_init(&skin);
    ASSERT_FALSE(skin_load(&skin, wsz));
    skin_destroy(&skin);
    free(wsz);
}

static void test_too_many_members_rejected(void)
{
    char *wsz = ka_path_join(g_dir, "many.wsz");
    ZipWriter zw;
    zw_open(&zw, wsz);
    for (int i = 0; i < 2001; i++) {
        char name[32];
        snprintf(name, sizeof(name), "f%04d.txt", i);
        zw_add(&zw, name, "x", 1, 0);
    }
    zw_close(&zw);

    Skin skin;
    skin_init(&skin);
    ASSERT_FALSE(skin_load(&skin, wsz));
    skin_destroy(&skin);
    free(wsz);
}

static void test_zip_reader_basics(void)
{
    char *path = ka_path_join(g_dir, "basic.zip");
    ZipWriter zw;
    zw_open(&zw, path);
    zw_add(&zw, "hello.txt", "hello world", 11, 0);
    zw_add(&zw, "/abs/path.txt", "x", 1, 0);
    zw_add(&zw, "sub/../up.txt", "x", 1, 0);
    zw_close(&zw);

    ZipArchive *za = zip_open(path);
    ASSERT_TRUE(za != NULL);
    ASSERT_EQ_INT(zip_count(za), 3);
    ASSERT_STR_EQ(zip_name(za, 0), "hello.txt");
    ASSERT_FALSE(zip_is_unsafe_name(za, 0));
    ASSERT_TRUE(zip_is_unsafe_name(za, 1));
    ASSERT_TRUE(zip_is_unsafe_name(za, 2));
    size_t len;
    uint8_t *data = zip_read(za, 0, &len);
    ASSERT_EQ_INT((int)len, 11);
    ASSERT_STR_EQ((char *)data, "hello world");
    free(data);
    zip_close(za);
    free(path);

    ASSERT_TRUE(zip_open("/nonexistent.zip") == NULL);
}

int main(void)
{
    g_dir = kt_tmpdir();
    RUN(test_viscolor_parsing);
    RUN(test_pledit_parsing);
    RUN(test_generate_and_load_default_skin);
    RUN(test_load_missing_dir_fails);
    RUN(test_wsz_loads);
    RUN(test_zip_bomb_rejected);
    RUN(test_too_many_members_rejected);
    RUN(test_zip_reader_basics);
    return kt_summary("test_skin");
}
