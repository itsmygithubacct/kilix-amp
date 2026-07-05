/* File dialogs (replaces QFileDialog): uses zenity when available, with a
 * text-entry path prompt as fallback. */
#ifndef KA_FILEDIALOG_H
#define KA_FILEDIALOG_H

#include "common.h"

/* Multi-select open. Returns heap array of heap paths (caller frees both);
 * *count set. NULL when cancelled. */
char **filedialog_open_files(const char *title, int *count);
/* Single-file open; heap string or NULL. */
char *filedialog_open_file(const char *title);
/* Save dialog; heap string or NULL. */
char *filedialog_save_file(const char *title, const char *suggested);
/* Directory chooser; heap string or NULL. */
char *filedialog_choose_dir(const char *title);

#endif
