/* Control-protocol JSON: writer escaping and flat-object reads. */
#include "ktest.h"

#include "json.h"

static void test_writer_shapes(void)
{
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_kv_int(&j, "protocol", 1);
    json_kv_bool(&j, "ok", true);
    json_kv_str(&j, "state", "playing");
    json_kv_num(&j, "pos", 12.5);
    json_obj_end(&j);
    ASSERT_STR_EQ(json_text(&j),
                  "{\"protocol\":1,\"ok\":true,\"state\":\"playing\","
                  "\"pos\":12.500}");
    json_free(&j);
}

static void test_writer_array(void)
{
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_arr_begin(&j, "items");
    json_arr_str(&j, "/music/a.ogg");
    json_arr_str(&j, "/music/b.ogg");
    json_arr_end(&j);
    json_kv_int(&j, "count", 2);
    json_obj_end(&j);
    ASSERT_STR_EQ(json_text(&j),
                  "{\"items\":[\"/music/a.ogg\",\"/music/b.ogg\"],"
                  "\"count\":2}");
    json_free(&j);

    JsonBuf empty = {0};
    json_obj_begin(&empty);
    json_arr_begin(&empty, "items");
    json_arr_end(&empty);
    json_obj_end(&empty);
    ASSERT_STR_EQ(json_text(&empty), "{\"items\":[]}");
    json_free(&empty);
}

/* A reply is one newline-delimited record, so nothing inside it may contain a
 * raw newline however strange the tag or file name is. */
static void test_writer_stays_one_line(void)
{
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_kv_str(&j, "title", "two\nlines\ttabbed \"quoted\" back\\slash");
    json_obj_end(&j);
    ASSERT_STR_EQ(json_text(&j),
                  "{\"title\":\"two\\nlines\\ttabbed \\\"quoted\\\" "
                  "back\\\\slash\"}");
    ASSERT_TRUE(strchr(json_text(&j), '\n') == NULL);
    json_free(&j);
}

static void test_writer_control_and_invalid_bytes(void)
{
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_kv_str(&j, "a", "\x01");
    /* A lone 0xFF is not UTF-8; a file name is only a byte string. */
    json_kv_str(&j, "b", "x\xffy");
    /* Valid multi-byte input survives untouched. */
    json_kv_str(&j, "c", "caf\xc3\xa9");
    json_obj_end(&j);
    ASSERT_STR_EQ(json_text(&j),
                  "{\"a\":\"\\u0001\",\"b\":\"x\\ufffdy\","
                  "\"c\":\"caf\xc3\xa9\"}");
    json_free(&j);
}

static void test_writer_rejects_non_finite(void)
{
    JsonBuf j = {0};
    json_obj_begin(&j);
    json_kv_num(&j, "pos", 0.0 / 0.0);
    json_obj_end(&j);
    ASSERT_STR_EQ(json_text(&j), "{\"pos\":0.000}");
    json_free(&j);
}

static void test_read_scalars(void)
{
    const char *req = "{\"cmd\":\"play\",\"protocol\":1,\"index\":3,"
                      "\"pos\":12.25,\"on\":true,\"off\":false}";
    char cmd[32];
    ASSERT_TRUE(json_get_str(req, "cmd", cmd, sizeof(cmd)));
    ASSERT_STR_EQ(cmd, "play");

    long long index = -1;
    ASSERT_TRUE(json_get_int(req, "index", &index));
    ASSERT_EQ_INT(index, 3);

    double pos = 0;
    ASSERT_TRUE(json_get_num(req, "pos", &pos));
    ASSERT_NEAR(pos, 12.25, 1e-9);

    bool flag = false;
    ASSERT_TRUE(json_get_bool(req, "on", &flag));
    ASSERT_TRUE(flag);
    ASSERT_TRUE(json_get_bool(req, "off", &flag));
    ASSERT_FALSE(flag);
}

static void test_read_missing_and_wrong_type(void)
{
    const char *req = "{\"cmd\":\"state\",\"index\":\"seven\"}";
    long long index = 42;
    /* A string where a number belongs must not be read as one. */
    ASSERT_FALSE(json_get_int(req, "index", &index));
    ASSERT_EQ_INT(index, 42);

    char missing[16] = "untouched";
    ASSERT_FALSE(json_get_str(req, "nope", missing, sizeof(missing)));
    ASSERT_STR_EQ(missing, "untouched");

    bool flag = true;
    ASSERT_FALSE(json_get_bool(req, "cmd", &flag));
    ASSERT_TRUE(flag);
}

/* A key nested inside another value must never be mistaken for a top-level
 * one, or a crafted request could steer a command it does not name. */
static void test_read_skips_nested(void)
{
    const char *req = "{\"cmd\":\"play\",\"meta\":{\"index\":99,"
                      "\"deep\":[{\"index\":98}]},\"index\":1}";
    long long index = -1;
    ASSERT_TRUE(json_get_int(req, "index", &index));
    ASSERT_EQ_INT(index, 1);

    const char *only_nested = "{\"meta\":{\"index\":99}}";
    index = -1;
    ASSERT_FALSE(json_get_int(only_nested, "index", &index));
    ASSERT_EQ_INT(index, -1);
}

/* A brace or a colon inside a string is data, not structure. */
static void test_read_string_punctuation(void)
{
    const char *req = "{\"path\":\"/music/{weird}: name.ogg\",\"index\":4}";
    char path[64];
    ASSERT_TRUE(json_get_str(req, "path", path, sizeof(path)));
    ASSERT_STR_EQ(path, "/music/{weird}: name.ogg");
    long long index = 0;
    ASSERT_TRUE(json_get_int(req, "index", &index));
    ASSERT_EQ_INT(index, 4);
}

static void test_read_escapes(void)
{
    const char *req = "{\"path\":\"a\\\\b\\\"c\\nd\\/e\\u0041\\u00e9\"}";
    char path[64];
    ASSERT_TRUE(json_get_str(req, "path", path, sizeof(path)));
    ASSERT_STR_EQ(path, "a\\b\"c\nd/eA\xc3\xa9");

    /* Surrogate pair -> one astral code point (U+1F3B5). */
    const char *pair = "{\"path\":\"\\ud83c\\udfb5\"}";
    ASSERT_TRUE(json_get_str(pair, "path", path, sizeof(path)));
    ASSERT_STR_EQ(path, "\xf0\x9f\x8e\xb5");

    /* A lone high surrogate is replaced rather than producing junk. */
    const char *lone = "{\"path\":\"\\ud83c\"}";
    ASSERT_TRUE(json_get_str(lone, "path", path, sizeof(path)));
    ASSERT_STR_EQ(path, "\xef\xbf\xbd");
}

static void test_read_truncates_safely(void)
{
    const char *req = "{\"path\":\"0123456789\"}";
    char small[5];
    ASSERT_TRUE(json_get_str(req, "path", small, sizeof(small)));
    ASSERT_STR_EQ(small, "0123");
}

static void test_read_malformed(void)
{
    char out[16];
    long long n = 7;
    ASSERT_FALSE(json_get_str("", "cmd", out, sizeof(out)));
    ASSERT_FALSE(json_get_str("not json", "cmd", out, sizeof(out)));
    ASSERT_FALSE(json_get_str("{\"cmd\":\"unterminated", "cmd", out,
                              sizeof(out)));
    ASSERT_FALSE(json_get_str("{\"cmd\"}", "cmd", out, sizeof(out)));
    ASSERT_FALSE(json_get_int("[1,2,3]", "cmd", &n));
    ASSERT_EQ_INT(n, 7);
}

int main(void)
{
    RUN(test_writer_shapes);
    RUN(test_writer_array);
    RUN(test_writer_stays_one_line);
    RUN(test_writer_control_and_invalid_bytes);
    RUN(test_writer_rejects_non_finite);
    RUN(test_read_scalars);
    RUN(test_read_missing_and_wrong_type);
    RUN(test_read_skips_nested);
    RUN(test_read_string_punctuation);
    RUN(test_read_escapes);
    RUN(test_read_truncates_safely);
    RUN(test_read_malformed);
    return kt_summary("json");
}
