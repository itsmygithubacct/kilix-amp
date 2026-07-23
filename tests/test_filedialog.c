/* File-dialog subprocess handling, exercised with a fake zenity binary. */
#include "ktest.h"

#include <sys/stat.h>

#include "filedialog.h"

static void test_large_multiselect_output_is_complete(void)
{
    char *dir = kt_tmpdir();
    char *zenity = ka_path_join(dir, "zenity");
    FILE *f = fopen(zenity, "w");
    ASSERT_TRUE(f != NULL);
    if (!f) {
        free(zenity);
        free(dir);
        return;
    }
    fputs("#!/bin/sh\n"
          "i=0\n"
          "while [ \"$i\" -lt 700 ]; do\n"
          "  printf '/tmp/kilix-selected-file-%04d-with-a-long-name.wav\\n' "
          "\"$i\"\n"
          "  i=$((i + 1))\n"
          "done\n",
          f);
    fclose(f);
    ASSERT_EQ_INT(chmod(zenity, 0700), 0);
    ASSERT_EQ_INT(setenv("PATH", dir, 1), 0);

    int count = 0;
    char **files = filedialog_open_files("Large selection", &count);
    ASSERT_TRUE(files != NULL);
    ASSERT_EQ_INT(count, 700);
    if (files && count == 700) {
        ASSERT_STR_EQ(files[0],
                      "/tmp/kilix-selected-file-0000-with-a-long-name.wav");
        ASSERT_STR_EQ(files[699],
                      "/tmp/kilix-selected-file-0699-with-a-long-name.wav");
    }
    for (int i = 0; i < count; i++)
        free(files[i]);
    free(files);
    free(zenity);
    free(dir);
}

int main(void)
{
    RUN(test_large_multiselect_output_is_complete);
    return kt_summary("test_filedialog");
}
