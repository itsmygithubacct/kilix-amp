/* Config persistence tests: typed getters/setters and INI roundtrip. */
#include "ktest.h"

#include "config.h"

static char *g_dir;

static void test_defaults(void)
{
    char *path = ka_path_join(g_dir, "fresh.ini");
    Config *c = config_open(path);
    ASSERT_EQ_INT(config_volume(c), 80);
    ASSERT_EQ_INT(config_balance(c), 0);
    ASSERT_FALSE(config_shuffle(c));
    ASSERT_EQ_INT(config_repeat(c), 0);
    ASSERT_FALSE(config_eq_enabled(c));
    ASSERT_TRUE(config_pl_visible(c));  /* default true */
    ASSERT_FALSE(config_eq_visible(c)); /* default false */
    ASSERT_EQ_INT(config_pl_height(c), 232);
    ASSERT_EQ_INT(config_ed_height(c), 300);
    ASSERT_STR_EQ(config_skin_path(c), "");
    int x, y;
    ASSERT_FALSE(config_get_window_pos(c, "main", &x, &y));
    config_close(c);
    free(path);
}

static void test_roundtrip(void)
{
    char *path = ka_path_join(g_dir, "rt.ini");
    Config *c = config_open(path);
    config_set_volume(c, 42);
    config_set_shuffle(c, true);
    config_set_repeat(c, 2);
    config_set_eq_preamp(c, -3.5);
    float bands[10] = {1, -2, 3, -4, 5, -6, 7, -8, 9, -10};
    config_set_eq_bands(c, bands);
    config_set_window_pos(c, "main", 123, 456);
    config_set_skin_path(c, "/some/skin.wsz");
    ASSERT_TRUE(config_sync(c));
    config_close(c);

    Config *c2 = config_open(path);
    ASSERT_EQ_INT(config_volume(c2), 42);
    ASSERT_TRUE(config_shuffle(c2));
    ASSERT_EQ_INT(config_repeat(c2), 2);
    ASSERT_NEAR(config_eq_preamp(c2), -3.5, 1e-9);
    float back[10];
    config_get_eq_bands(c2, back);
    for (int i = 0; i < 10; i++)
        ASSERT_NEAR(back[i], bands[i], 1e-6);
    int x, y;
    ASSERT_TRUE(config_get_window_pos(c2, "main", &x, &y));
    ASSERT_EQ_INT(x, 123);
    ASSERT_EQ_INT(y, 456);
    ASSERT_STR_EQ(config_skin_path(c2), "/some/skin.wsz");
    config_close(c2);
    free(path);
}

static void test_bool_forms(void)
{
    char *path = ka_path_join(g_dir, "bools.ini");
    FILE *f = fopen(path, "w");
    fputs("[a]\nt1=true\nt2=1\nt3=YES\nf1=false\nf2=0\nf3=whatever\n", f);
    fclose(f);
    Config *c = config_open(path);
    ASSERT_TRUE(config_get_bool(c, "a/t1", false));
    ASSERT_TRUE(config_get_bool(c, "a/t2", false));
    ASSERT_TRUE(config_get_bool(c, "a/t3", false));
    ASSERT_FALSE(config_get_bool(c, "a/f1", true));
    ASSERT_FALSE(config_get_bool(c, "a/f2", true));
    ASSERT_FALSE(config_get_bool(c, "a/f3", true));
    ASSERT_TRUE(config_get_bool(c, "a/missing", true));
    config_close(c);
    free(path);
}

static void test_malformed_values(void)
{
    char *path = ka_path_join(g_dir, "mal.ini");
    FILE *f = fopen(path, "w");
    fputs("[x]\nnum=notanumber\nflt=alsobad\n", f);
    fclose(f);
    Config *c = config_open(path);
    ASSERT_EQ_INT(config_get_int(c, "x/num", 7), 7);
    ASSERT_NEAR(config_get_float(c, "x/flt", 2.5), 2.5, 1e-9);
    config_close(c);
    free(path);
}

static void test_default_path_honors_xdg_config_home(void)
{
    const char *old = getenv("XDG_CONFIG_HOME");
    char *saved = old ? ka_strdup(old) : NULL;
    char *xdg = ka_path_join(g_dir, "xdg-config");
    setenv("XDG_CONFIG_HOME", xdg, 1);

    Config *c = config_open(NULL);
    config_set_volume(c, 37);
    ASSERT_TRUE(config_sync(c));
    config_close(c);

    char *app_dir = ka_path_join(xdg, "kilix-amp");
    char *path = ka_path_join(app_dir, "kilix-amp.ini");
    ASSERT_TRUE(ka_is_file(path));
    Config *saved_config = config_open(path);
    ASSERT_EQ_INT(config_volume(saved_config), 37);
    config_close(saved_config);

    if (saved)
        setenv("XDG_CONFIG_HOME", saved, 1);
    else
        unsetenv("XDG_CONFIG_HOME");
    free(saved);
    free(path);
    free(app_dir);
    free(xdg);
}

int main(void)
{
    g_dir = kt_tmpdir();
    RUN(test_defaults);
    RUN(test_roundtrip);
    RUN(test_bool_forms);
    RUN(test_malformed_values);
    RUN(test_default_path_honors_xdg_config_home);
    return kt_summary("test_config");
}
