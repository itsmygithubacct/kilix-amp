/* Load .wsz/.zip skins or skin directories, parse BMPs and config files.
 * Port of nixamp lib/skin_engine.py on SDL_Surface instead of QPixmap. */
#ifndef KA_SKIN_H
#define KA_SKIN_H

#include <SDL.h>

#include "common.h"
#include "consts.h"

typedef struct {
    KColor viscolors[VISCOLOR_COUNT];
    KColor pl_normal;
    KColor pl_current;
    KColor pl_normalbg;
    KColor pl_selectedbg;
    char pl_font[64];
} SkinColors;

void skin_colors_init(SkinColors *sc); /* defaults */
void skin_colors_parse_viscolor(SkinColors *sc, const char *text);
void skin_colors_parse_pledit(SkinColors *sc, const char *text);
KColor skin_colors_vis(const SkinColors *sc, int index); /* clamped */

typedef struct {
    /* Indexed parallel to SKIN_BITMAPS ("main.bmp" -> 0 ...); NULL if the
     * skin lacks that sheet. All surfaces are converted to ARGB8888. */
    SDL_Surface *bitmaps[SKIN_BITMAP_COUNT];
    SkinColors colors;
    char *skin_path;
} Skin;

/* Extraction safety caps to guard against zip bombs. */
#define SKIN_MAX_UNCOMPRESSED_BYTES (200u * 1024 * 1024)
#define SKIN_MAX_MEMBERS 2000

void skin_init(Skin *s);
/* Load from .wsz/.zip or a directory. Frees previous bitmaps first.
 * Returns false if nothing loadable was found (state then holds defaults). */
bool skin_load(Skin *s, const char *path);
void skin_destroy(Skin *s);

/* Bitmap by name without extension ("main", "cbuttons", ...); NULL if
 * missing. */
SDL_Surface *skin_bitmap(const Skin *s, const char *name);
int skin_bitmap_index(const char *name); /* -1 if unknown */

#endif
