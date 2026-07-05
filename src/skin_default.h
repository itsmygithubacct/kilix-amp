/* Programmatic default skin generator - classic dark charcoal Winamp
 * aesthetic. Port of nixamp lib/skin_default.py using the in-house 4x6
 * pixel font in place of Qt font rendering. */
#ifndef KA_SKIN_DEFAULT_H
#define KA_SKIN_DEFAULT_H

#include "common.h"

/* Write all skin bitmaps + config files into skin_dir (created if needed).
 * Returns false if the directory could not be created or a file failed to
 * save. */
bool skin_default_generate(const char *skin_dir);

#endif
