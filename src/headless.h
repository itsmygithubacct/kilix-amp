/* Headless backend: the same decoder, EQ and playlist the windowed player
 * uses, driven over the control socket instead of a skin.
 *
 * There is one playback implementation in this program, not two. `--headless`
 * skips SDL video, the skin and every window; everything below the UI is
 * shared, so a track sounds the same whichever front end asked for it. */
#ifndef KA_HEADLESS_H
#define KA_HEADLESS_H

#include "common.h"

/* Runs until a `quit` command, SIGINT/SIGTERM, or KILIXAMP_EXIT_AFTER_MS.
 * `socket_path` may be NULL for control_default_socket_path().
 * Returns a process exit status. */
int headless_run(const char *const *files, int n_files,
                 const char *socket_path);

#endif
