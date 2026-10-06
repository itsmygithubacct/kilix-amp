/* A long TMPDIR: the test helpers stay inside it and never truncate a path, and
 * the control suite still passes when TMPDIR leaves no room for an absolute
 * Unix socket path (sockaddr_un.sun_path holds 108 bytes). */
#include "ktest.h"
#include "common.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <ftw.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

/* Longer than any absolute socket path, and longer than the 95 bytes at which
 * a truncated path used to land outside TMPDIR. */
#define LONG_TMPDIR_BYTES 117u

static int remove_entry(const char *path, const struct stat *st, int type, struct FTW *w)
{
    (void)st; (void)type; (void)w;
    return remove(path);
}

/* Entries of `dir` that look like this suite's own files, apart from `except`. */
static int count_artefacts(const char *dir, const char *except)
{
    DIR *d = opendir(dir);
    int count = 0;
    if (d == NULL)
        return -1;
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (except != NULL && strcmp(e->d_name, except) == 0)
            continue;
        if (fnmatch("*.sock", e->d_name, 0) == 0 || fnmatch("kt.*", e->d_name, 0) == 0
            || strcmp(e->d_name, "keep.txt") == 0 || strcmp(e->d_name, "target.txt") == 0)
            count++;
    }
    closedir(d);
    return count;
}

/* Names in `dir`, other than . and .., into `names`. */
static int list_dir(const char *dir, char names[][NAME_MAX + 1], int cap)
{
    DIR *d = opendir(dir);
    int count = 0;
    if (d == NULL)
        return -1;
    for (struct dirent *e; (e = readdir(d)) != NULL;) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        if (count < cap)
            snprintf(names[count], NAME_MAX + 1, "%s", e->d_name);
        count++;
    }
    closedir(d);
    return count;
}

static void test_helpers_in_long_tmpdir(const char *long_dir)
{
    setenv("TMPDIR", long_dir, 1);
    char *dir = kt_tmpdir();
    struct stat st;
    ASSERT_EQ_INT(strncmp(dir, long_dir, strlen(long_dir)), 0);
    ASSERT_EQ_INT(dir[strlen(long_dir)], '/');
    ASSERT_EQ_INT(strncmp(dir + strlen(long_dir) + 1, "kt.", 3), 0);
    ASSERT_EQ_INT(stat(dir, &st), 0);
    ASSERT_TRUE(S_ISDIR(st.st_mode));
    ASSERT_EQ_INT(st.st_mode & 0777, 0700);
    ASSERT_TRUE(strlen(dir) >= KT_PATH_CAP);

    /* No absolute socket path fits, so the helper names the file relative to
     * the entered directory, and the file lands inside the directory. */
    kt_enter(dir);
    char path[KT_PATH_CAP];
    kt_path(path, sizeof(path), dir, "rt.sock");
    ASSERT_STR_EQ(path, "rt.sock");
    FILE *f = fopen(path, "w");
    ASSERT_TRUE(f != NULL);
    if (f)
        fclose(f);
    char absolute[PATH_MAX];
    snprintf(absolute, sizeof(absolute), "%s/rt.sock", dir);
    ASSERT_EQ_INT(stat(absolute, &st), 0);

    /* A path that fits stays absolute. */
    kt_path(path, sizeof(path), "/x", "n");
    ASSERT_STR_EQ(path, "/x/n");
    free(dir);
}

static void test_control_in_long_tmpdir(const char *long_dir)
{
    char exe[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1u);
    ASSERT_TRUE(n > 0);
    if (n <= 0)
        return;
    exe[n] = '\0';
    char *slash = strrchr(exe, '/');
    ASSERT_TRUE(slash != NULL);
    if (slash == NULL)
        return;
    snprintf(slash + 1, sizeof(exe) - (size_t)(slash + 1 - exe), "test_control");

    char out[PATH_MAX], tmpdir_env[PATH_MAX + 16];
    snprintf(out, sizeof(out), "%s/control.out", long_dir);
    snprintf(tmpdir_env, sizeof(tmpdir_env), "TMPDIR=%s", long_dir);
    pid_t child = fork();
    if (child == 0) {
        int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (fd < 0 || dup2(fd, 1) < 0)
            _exit(125);
        char *env[] = {tmpdir_env, "PATH=/usr/bin:/bin", NULL};
        execle(exe, exe, (char *)NULL, env);
        _exit(126);
    }
    int status = -1;
    ASSERT_TRUE(child > 0 && waitpid(child, &status, 0) == child);
    ASSERT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);

    char *text = ka_read_file(out, NULL);
    int checks = 0, failures = -1;
    ASSERT_TRUE(text != NULL);
    const char *summary = text ? strstr(text, "control: ") : NULL;
    ASSERT_TRUE(summary != NULL);
    if (summary)
        ASSERT_EQ_INT(sscanf(summary, "control: %d checks, %d failures", &checks, &failures), 2);
    ASSERT_TRUE(checks >= 100);
    ASSERT_EQ_INT(failures, 0);
    free(text);
}

/* Nothing may be written above the long directory: every level of the chain
 * holds only the next level, and the real TMPDIR gained nothing of ours. */
static void test_nothing_escapes(const char *outer, const char *long_dir, const char *real_tmp,
    const char *outer_name, int outside_before)
{
    char names[8][NAME_MAX + 1];
    char level[PATH_MAX];
    snprintf(level, sizeof(level), "%s", outer);
    size_t length = strlen(outer);
    while (length < strlen(long_dir)) {
        ASSERT_EQ_INT(list_dir(level, names, 8), 1);
        const char *rest = long_dir + length + 1;
        size_t part = strcspn(rest, "/");
        ASSERT_EQ_INT(strncmp(names[0], rest, part), 0);
        ASSERT_EQ_INT(names[0][part], '\0');
        length += 1u + part;
        snprintf(level, sizeof(level), "%.*s", (int)length, long_dir);
    }
    /* The helper's own directory and test_control's, and the captured output. */
    ASSERT_EQ_INT(list_dir(long_dir, names, 8), 3);
    ASSERT_EQ_INT(count_artefacts(long_dir, NULL), 2);
    ASSERT_EQ_INT(count_artefacts(real_tmp, outer_name), outside_before);
}

int main(void)
{
    const char *real_tmp = getenv("TMPDIR");
    if (real_tmp == NULL || real_tmp[0] == '\0')
        real_tmp = "/tmp";
    char *outer = kt_tmpdir();
    const char *outer_name = strrchr(outer, '/') + 1;
    int outside_before = count_artefacts(real_tmp, outer_name);

    char long_dir[PATH_MAX];
    snprintf(long_dir, sizeof(long_dir), "%s", outer);
    while (strlen(long_dir) < LONG_TMPDIR_BYTES) {
        size_t length = strlen(long_dir);
        snprintf(long_dir + length, sizeof(long_dir) - length, "/long-tmpdir-component");
        if (mkdir(long_dir, 0700) != 0) {
            perror("mkdir");
            return 1;
        }
    }
    char *real_copy = strdup(real_tmp);

    printf("  test_helpers_in_long_tmpdir\n");
    test_helpers_in_long_tmpdir(long_dir);
    printf("  test_control_in_long_tmpdir\n");
    test_control_in_long_tmpdir(long_dir);
    printf("  test_nothing_escapes\n");
    test_nothing_escapes(outer, long_dir, real_copy, outer_name, outside_before);

    int rc = kt_summary("long tmpdir");
    if (chdir("/") == 0)
        nftw(outer, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
    free(real_copy);
    free(outer);
    return rc;
}
