/* Minimal single-file test harness for kilix-amp unit tests. */
#ifndef KTEST_H
#define KTEST_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int kt_checks = 0;
static int kt_failures = 0;

#define KT_FAIL(fmt, ...)                                                   \
    do {                                                                    \
        kt_failures++;                                                      \
        printf("    FAIL %s:%d: " fmt "\n", __FILE__, __LINE__,             \
               ##__VA_ARGS__);                                              \
    } while (0)

#define ASSERT_TRUE(x)                                                      \
    do {                                                                    \
        kt_checks++;                                                        \
        if (!(x))                                                           \
            KT_FAIL("expected true: %s", #x);                               \
    } while (0)

#define ASSERT_FALSE(x)                                                     \
    do {                                                                    \
        kt_checks++;                                                        \
        if (x)                                                              \
            KT_FAIL("expected false: %s", #x);                              \
    } while (0)

#define ASSERT_EQ_INT(a, b)                                                 \
    do {                                                                    \
        kt_checks++;                                                        \
        long long kt_a = (long long)(a), kt_b = (long long)(b);             \
        if (kt_a != kt_b)                                                   \
            KT_FAIL("%s == %lld, expected %s == %lld", #a, kt_a, #b, kt_b); \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                 \
    do {                                                                    \
        kt_checks++;                                                        \
        const char *kt_a = (a), *kt_b = (b);                                \
        if (strcmp(kt_a, kt_b) != 0)                                        \
            KT_FAIL("%s == \"%s\", expected \"%s\"", #a, kt_a, kt_b);       \
    } while (0)

#define ASSERT_NEAR(a, b, eps)                                              \
    do {                                                                    \
        kt_checks++;                                                        \
        double kt_a = (a), kt_b = (b);                                      \
        if (fabs(kt_a - kt_b) > (eps))                                      \
            KT_FAIL("%s == %g, expected %g +/- %g", #a, kt_a, kt_b,         \
                    (double)(eps));                                         \
    } while (0)

#define RUN(fn)                                                             \
    do {                                                                    \
        printf("  %s\n", #fn);                                              \
        fn();                                                               \
    } while (0)

static inline int kt_summary(const char *suite)
{
    printf("%s: %d checks, %d failures\n", suite, kt_checks, kt_failures);
    return kt_failures ? 1 : 0;
}

/* Temp dir helper: under $TMPDIR when set, so a sandboxed run stays inside
 * its sandbox; /tmp otherwise. */
static inline char *kt_tmpdir(void)
{
    const char *root = getenv("TMPDIR");
    if (root == NULL || root[0] == '\0')
        root = "/tmp";
    size_t size = strlen(root) + sizeof("/kilixamp_test_XXXXXX");
    char *copy = malloc(size);
    if (copy == NULL) {
        perror("malloc");
        exit(1);
    }
    snprintf(copy, size, "%s/kilixamp_test_XXXXXX", root);
    if (!mkdtemp(copy)) {
        perror("mkdtemp");
        exit(1);
    }
    return copy;
}

#endif
