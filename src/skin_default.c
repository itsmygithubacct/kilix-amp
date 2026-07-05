#include "skin_default.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "consts.h"
#include "font4x6.h"
#include "render.h"

/* Colors for the default skin */
#define BG_DARK        ((KColor){35, 36, 40})
#define BG_MID         ((KColor){50, 52, 58})
#define BG_LIGHT       ((KColor){65, 68, 75})
#define BEVEL_LIGHT    ((KColor){85, 88, 95})
#define BEVEL_DARK     ((KColor){20, 21, 24})
#define GREEN_LCD      ((KColor){0, 198, 0})
#define GREEN_DIM      ((KColor){0, 80, 0})
#define GREEN_BRIGHT   ((KColor){0, 255, 0})
#define TEXT_GRAY      ((KColor){180, 180, 180})
#define BUTTON_FACE    ((KColor){58, 60, 66})
#define BUTTON_PRESSED ((KColor){40, 42, 46})
#define ACCENT         ((KColor){0, 150, 0})
#define BLACK          ((KColor){0, 0, 0})
#define WHITE          ((KColor){255, 255, 255})
#define BG_INACTIVE    ((KColor){42, 44, 50})

/* QColor.darker(130) equivalent: scale channels by 100/130. */
static KColor darker(KColor c, int factor)
{
    return (KColor){(uint8_t)(c.r * 100 / factor),
                    (uint8_t)(c.g * 100 / factor),
                    (uint8_t)(c.b * 100 / factor)};
}

/* Draw a 3D beveled edge. */
static void draw_bevel(SDL_Surface *s, int x, int y, int w, int h,
                       bool raised)
{
    KColor lt = raised ? BEVEL_LIGHT : BEVEL_DARK;
    KColor dk = raised ? BEVEL_DARK : BEVEL_LIGHT;
    ksurf_line(s, x, y, x + w - 1, y, lt);         /* top */
    ksurf_line(s, x, y, x, y + h - 1, lt);         /* left */
    ksurf_line(s, x + w - 1, y, x + w - 1, y + h - 1, dk); /* right */
    ksurf_line(s, x, y + h - 1, x + w - 1, y + h - 1, dk); /* bottom */
}

/* Draw a button with bevel and optional label. */
static void draw_button(SDL_Surface *s, int x, int y, int w, int h,
                        bool pressed, const char *label)
{
    ksurf_fill(s, KRECT(x, y, w, h), pressed ? BUTTON_PRESSED : BUTTON_FACE);
    draw_bevel(s, x, y, w, h, !pressed);
    if (label && *label)
        font4x6_draw_centered(s, KRECT(x, y, w, h), label, TEXT_GRAY);
}

static bool save(SDL_Surface *s, const char *dir, const char *name)
{
    char *path = ka_path_join(dir, name);
    int rc = SDL_SaveBMP(s, path);
    free(path);
    SDL_FreeSurface(s);
    return rc == 0;
}

/* main.bmp - 275x116 main window background. */
static bool gen_main(const char *dir)
{
    SDL_Surface *s = ksurf_new(275, 116, BG_DARK);
    draw_bevel(s, 0, 0, 275, 116, true);
    ksurf_fill(s, KRECT(0, 0, 275, 14), BG_MID);   /* title bar base */
    ksurf_fill(s, KRECT(10, 22, 8, 43), BG_MID);   /* clutterbar */
    ksurf_fill(s, KRECT(24, 43, 76, 16), BLACK);   /* vis area */
    draw_bevel(s, 23, 42, 78, 18, false);
    ksurf_fill(s, KRECT(111, 24, 154, 6), BLACK);  /* title text */
    ksurf_fill(s, KRECT(36, 26, 66, 13), BLACK);   /* time display */
    draw_bevel(s, 35, 25, 68, 15, false);
    ksurf_fill(s, KRECT(111, 43, 60, 8), BG_DARK); /* kbps/khz */
    ksurf_fill(s, KRECT(107, 57, 68, 13), BG_MID); /* volume */
    ksurf_fill(s, KRECT(177, 57, 38, 13), BG_MID); /* balance */
    ksurf_fill(s, KRECT(16, 88, 145, 18), BG_MID); /* transport */
    ksurf_fill(s, KRECT(16, 72, 248, 10), BG_MID); /* posbar */
    ksurf_fill(s, KRECT(212, 41, 56, 12), BG_DARK);/* mono/stereo */
    return save(s, dir, "main.bmp");
}

/* titlebar.bmp - title bar states (active/inactive) + buttons. */
static bool gen_titlebar(const char *dir)
{
    SDL_Surface *s = ksurf_new(335, 36, BG_DARK);
    struct { int row; bool active; } rows[] = {{0, true}, {15, false}};
    for (int i = 0; i < 2; i++) {
        int row = rows[i].row;
        ksurf_fill(s, KRECT(27, row, 275, 14),
                   rows[i].active ? BG_MID : BG_INACTIVE);
        draw_bevel(s, 27, row, 275, 14, true);
        KColor c = rows[i].active ? GREEN_LCD : GREEN_DIM;
        font4x6_draw(s, 27 + 30, row + 4, "KILIX-AMP", c);
    }
    /* Close button normal (18,0) and pressed (18,9) */
    draw_button(s, 18, 0, 9, 9, false, "X");
    draw_button(s, 18, 9, 9, 9, true, "X");
    /* Minimize/shade at (0,18) normal, (9,18) pressed */
    draw_button(s, 0, 18, 9, 9, false, "_");
    draw_button(s, 9, 18, 9, 9, true, "_");
    return save(s, dir, "titlebar.bmp");
}

/* cbuttons.bmp - transport buttons (prev/play/pause/stop/next/eject). */
static bool gen_cbuttons(const char *dir)
{
    SDL_Surface *s = ksurf_new(136, 36, BG_DARK);
    const char *labels[] = {"|<", ">", "||", "[]", ">|"};
    for (int i = 0; i < 5; i++) {
        int w = (i == 4) ? 22 : 23;
        int x = (i == 4) ? 92 : i * 23;
        draw_button(s, x, 0, w, 18, false, labels[i]);
        draw_button(s, x, 18, w, 18, true, labels[i]);
    }
    draw_button(s, 114, 0, 22, 16, false, "^");
    draw_button(s, 114, 16, 22, 16, true, "^");
    return save(s, dir, "cbuttons.bmp");
}

/* posbar.bmp - position/seek bar background and thumb. */
static bool gen_posbar(const char *dir)
{
    SDL_Surface *s = ksurf_new(307, 10, BG_MID);
    ksurf_fill(s, KRECT(0, 0, 248, 10), BG_MID);
    draw_bevel(s, 0, 0, 248, 10, false);
    ksurf_fill(s, KRECT(3, 4, 242, 2), BEVEL_DARK); /* groove */

    ksurf_fill(s, KRECT(248, 0, 29, 10), BUTTON_FACE); /* thumb normal */
    draw_bevel(s, 248, 0, 29, 10, true);
    ksurf_line(s, 262, 2, 262, 7, BEVEL_DARK);

    ksurf_fill(s, KRECT(278, 0, 29, 10), BUTTON_PRESSED); /* pressed */
    draw_bevel(s, 278, 0, 29, 10, false);
    ksurf_line(s, 292, 2, 292, 7, BEVEL_LIGHT);
    return save(s, dir, "posbar.bmp");
}

/* volume.bmp - 28 frames of 68x13 background + thumb. */
static bool gen_volume(const char *dir)
{
    SDL_Surface *s = ksurf_new(68, 433, BG_MID);
    for (int i = 0; i < 28; i++) {
        int y = i * 13;
        ksurf_fill(s, KRECT(0, y, 68, 13), BG_MID);
        draw_bevel(s, 0, y, 68, 13, false);
        ksurf_line(s, 3, y + 6, 64, y + 6, BEVEL_DARK);
        int fill_w = i * 60 / 27;
        ksurf_fill(s, KRECT(3, y + 4, fill_w, 5), GREEN_DIM);
    }
    ksurf_fill(s, KRECT(0, 422, 14, 11), BUTTON_PRESSED);
    draw_bevel(s, 0, 422, 14, 11, false);
    ksurf_fill(s, KRECT(15, 422, 14, 11), BUTTON_FACE);
    draw_bevel(s, 15, 422, 14, 11, true);
    return save(s, dir, "volume.bmp");
}

/* balance.bmp - 28 frames of 38x13 background + thumb. */
static bool gen_balance(const char *dir)
{
    SDL_Surface *s = ksurf_new(38, 433, BG_MID);
    for (int i = 0; i < 28; i++) {
        int y = i * 13;
        ksurf_fill(s, KRECT(0, y, 38, 13), BG_MID);
        draw_bevel(s, 0, y, 38, 13, false);
        ksurf_line(s, 3, y + 6, 34, y + 6, BEVEL_DARK);
        ksurf_line(s, 19, y + 3, 19, y + 9, GREEN_DIM); /* center marker */
    }
    ksurf_fill(s, KRECT(0, 422, 14, 11), BUTTON_PRESSED);
    draw_bevel(s, 0, 422, 14, 11, false);
    ksurf_fill(s, KRECT(15, 422, 14, 11), BUTTON_FACE);
    draw_bevel(s, 15, 422, 14, 11, true);
    return save(s, dir, "balance.bmp");
}

/* shufrep.bmp - shuffle/repeat/EQ/PL toggle buttons. */
static bool gen_shufrep(const char *dir)
{
    SDL_Surface *s = ksurf_new(92, 86, BG_DARK);
    struct { int y; bool on, pressed; } states[] = {
        {0, true, false}, {15, true, true},
        {30, false, false}, {45, false, true},
    };
    for (int i = 0; i < 4; i++) {
        bool on = states[i].on, pressed = states[i].pressed;
        int y = states[i].y;
        KColor face = pressed ? BUTTON_PRESSED : (on ? ACCENT : BUTTON_FACE);
        KColor text = on ? WHITE : TEXT_GRAY;
        ksurf_fill(s, KRECT(0, y, 28, 15), face);
        draw_bevel(s, 0, y, 28, 15, !pressed);
        font4x6_draw_centered(s, KRECT(0, y, 28, 15), "R", text);
        ksurf_fill(s, KRECT(28, y, 46, 15), face);
        draw_bevel(s, 28, y, 46, 15, !pressed);
        font4x6_draw_centered(s, KRECT(28, y, 46, 15), "S", text);
    }

    /* EQ/PL on states at y=61: normal at col, pressed at col+46 */
    struct { int col; const char *label; } eqpl[] = {{0, "EQ"}, {23, "PL"}};
    for (int i = 0; i < 2; i++) {
        int col = eqpl[i].col;
        const char *label = eqpl[i].label;
        ksurf_fill(s, KRECT(col, 61, 23, 12), ACCENT);
        draw_bevel(s, col, 61, 23, 12, true);
        font4x6_draw_centered(s, KRECT(col, 61, 23, 12), label, WHITE);
        ksurf_fill(s, KRECT(col + 46, 61, 23, 12), darker(ACCENT, 130));
        draw_bevel(s, col + 46, 61, 23, 12, false);
        font4x6_draw_centered(s, KRECT(col + 46, 61, 23, 12), label, WHITE);
        /* off states at y=73 */
        ksurf_fill(s, KRECT(col, 73, 23, 12), BUTTON_FACE);
        draw_bevel(s, col, 73, 23, 12, true);
        font4x6_draw_centered(s, KRECT(col, 73, 23, 12), label, TEXT_GRAY);
        ksurf_fill(s, KRECT(col + 46, 73, 23, 12), BUTTON_PRESSED);
        draw_bevel(s, col + 46, 73, 23, 12, false);
        font4x6_draw_centered(s, KRECT(col + 46, 73, 23, 12), label,
                              TEXT_GRAY);
    }
    return save(s, dir, "shufrep.bmp");
}

/* nums_ex.bmp - LCD digits 0-9, blank, minus; each 9x13. */
static bool gen_nums_ex(const char *dir)
{
    SDL_Surface *s = ksurf_new(12 * 9, 13, BLACK);
    const char *chars = LCD_DIGITS;
    /* Draw each 4x6 glyph doubled to 8x12 for an LCD-sized digit. */
    for (int i = 0; chars[i]; i++) {
        const uint8_t *rows = font4x6_glyph((unsigned char)chars[i]);
        if (!rows)
            continue;
        int ox = i * 9;
        for (int ry = 0; ry < 6; ry++)
            for (int rx = 0; rx < 4; rx++)
                if (rows[ry] & (8 >> rx))
                    ksurf_fill(s, KRECT(ox + rx * 2, ry * 2 + 1, 2, 2),
                               GREEN_LCD);
    }
    return save(s, dir, "nums_ex.bmp");
}

/* text.bmp - bitmap font, 5x6 characters in grid layout. */
static bool gen_text(const char *dir)
{
    int cols = 31, rows = 3;
    SDL_Surface *s = ksurf_new(cols * 5, rows * 6, BLACK);
    const char *row_chars[3] = {FONT_CHARS_ROW0, FONT_CHARS_ROW1,
                                FONT_CHARS_ROW2};
    for (int r = 0; r < 3; r++) {
        const char *chars = row_chars[r];
        for (int i = 0; chars[i]; i++) {
            char one[2] = {chars[i], 0};
            font4x6_draw(s, i * 5, r * 6, one, GREEN_LCD);
        }
    }
    return save(s, dir, "text.bmp");
}

/* playpaus.bmp - play/pause/stop status icons, each 9x9. */
static bool gen_playpaus(const char *dir)
{
    SDL_Surface *s = ksurf_new(27, 9, BLACK);
    /* Play arrow: triangle (1,0) (1,8) (8,4) */
    for (int x = 1; x <= 8; x++) {
        double t = (x - 1) * 4.0 / 7.0;
        int y0 = (int)(t + 0.5);
        int y1 = (int)(8.0 - t + 0.5);
        ksurf_line(s, x, y0, x, y1, GREEN_LCD);
    }
    /* Pause bars */
    ksurf_fill(s, KRECT(10, 0, 3, 9), GREEN_LCD);
    ksurf_fill(s, KRECT(15, 0, 3, 9), GREEN_LCD);
    /* Stop square */
    ksurf_fill(s, KRECT(19, 1, 7, 7), GREEN_LCD);
    return save(s, dir, "playpaus.bmp");
}

/* monoster.bmp - mono/stereo indicators. */
static bool gen_monoster(const char *dir)
{
    SDL_Surface *s = ksurf_new(56, 24, BG_DARK);
    font4x6_draw_centered(s, KRECT(0, 0, 29, 12), "ST", GREEN_LCD);
    font4x6_draw_centered(s, KRECT(29, 0, 27, 12), "MO", GREEN_LCD);
    font4x6_draw_centered(s, KRECT(0, 12, 29, 12), "ST", GREEN_DIM);
    font4x6_draw_centered(s, KRECT(29, 12, 27, 12), "MO", GREEN_DIM);
    return save(s, dir, "monoster.bmp");
}

/* eqmain.bmp - equalizer window background and elements. */
static bool gen_eqmain(const char *dir)
{
    SDL_Surface *s = ksurf_new(275, 190, BG_DARK);
    ksurf_fill(s, KRECT(0, 0, 275, 116), BG_DARK);
    draw_bevel(s, 0, 0, 275, 116, true);
    ksurf_fill(s, KRECT(0, 0, 275, 14), BG_MID);
    ksurf_fill(s, KRECT(86, 17, 113, 19), BLACK); /* graph area */
    draw_bevel(s, 85, 16, 115, 21, false);

    const char *freqs[] = {"60", "170", "310", "600", "1K",
                           "3K", "6K", "12K", "14K", "16K"};
    for (int i = 0; i < 10; i++)
        font4x6_draw_centered(s, KRECT(EQ_BAND_X[i] - 5, 102, 24, 10),
                              freqs[i], TEXT_GRAY);

    /* Slider grooves */
    ksurf_line(s, EQ_PREAMP_X + 6, EQ_SLIDER_Y, EQ_PREAMP_X + 6,
               EQ_SLIDER_Y + EQ_SLIDER_H, BEVEL_DARK);
    for (int i = 0; i < 10; i++)
        ksurf_line(s, EQ_BAND_X[i] + 6, EQ_SLIDER_Y, EQ_BAND_X[i] + 6,
                   EQ_SLIDER_Y + EQ_SLIDER_H, BEVEL_DARK);

    /* ON/AUTO labels on the background */
    font4x6_draw_centered(s, KRECT(14, 18, 25, 12), "ON", TEXT_GRAY);
    font4x6_draw_centered(s, KRECT(39, 18, 33, 12), "AUTO", TEXT_GRAY);

    /* Row 119: ON/AUTO button sources */
    draw_button(s, 10, 119, 25, 12, false, "ON");
    draw_button(s, 36, 119, 25, 12, false, "ON");
    draw_button(s, 61, 119, 33, 12, false, "AUTO");
    draw_button(s, 94, 119, 33, 12, false, "AUTO");

    /* Title bars at rows 134 (active) and 149 (inactive) */
    ksurf_fill(s, KRECT(0, 134, 275, 14), BG_MID);
    draw_bevel(s, 0, 134, 275, 14, true);
    font4x6_draw(s, 30, 138, "EQUALIZER", GREEN_LCD);
    ksurf_fill(s, KRECT(0, 149, 275, 14), BG_INACTIVE);
    draw_bevel(s, 0, 149, 275, 14, true);
    font4x6_draw(s, 30, 153, "EQUALIZER", GREEN_DIM);

    /* Slider thumbs at (0,164) normal, (0,176) pressed */
    ksurf_fill(s, KRECT(0, 164, 14, 11), BUTTON_FACE);
    draw_bevel(s, 0, 164, 14, 11, true);
    ksurf_fill(s, KRECT(0, 176, 14, 11), BUTTON_PRESSED);
    draw_bevel(s, 0, 176, 14, 11, false);

    /* Presets button at (224,164) normal, (224,176) pressed */
    draw_button(s, 224, 164, 44, 12, false, "PRESETS");
    draw_button(s, 224, 176, 44, 12, true, "PRESETS");
    return save(s, dir, "eqmain.bmp");
}

/* eq_ex.bmp - extended EQ elements (shade mode). */
static bool gen_eq_ex(const char *dir)
{
    SDL_Surface *s = ksurf_new(275, 14, BG_MID);
    draw_bevel(s, 0, 0, 275, 14, true);
    font4x6_draw(s, 30, 4, "EQ", GREEN_LCD);
    return save(s, dir, "eq_ex.bmp");
}

/* pledit.bmp - playlist window elements (corners, edges, buttons). */
static bool gen_pledit(const char *dir)
{
    SDL_Surface *s = ksurf_new(280, 150, BG_DARK);

    /* Top-left corners */
    ksurf_fill(s, KRECT(0, 0, 25, 20), BG_MID);
    draw_bevel(s, 0, 0, 25, 20, true);
    ksurf_fill(s, KRECT(0, 21, 25, 20), BG_INACTIVE);
    draw_bevel(s, 0, 21, 25, 20, true);

    /* Title stretch pieces */
    ksurf_fill(s, KRECT(26, 0, 100, 20), BG_MID);
    font4x6_draw_centered(s, KRECT(26, 0, 100, 20), "PLAYLIST", GREEN_LCD);
    ksurf_fill(s, KRECT(26, 21, 100, 20), BG_INACTIVE);
    font4x6_draw_centered(s, KRECT(26, 21, 100, 20), "PLAYLIST", GREEN_DIM);

    /* Top-right (with close button) */
    ksurf_fill(s, KRECT(153, 0, 125, 20), BG_MID);
    draw_bevel(s, 153, 0, 125, 20, true);
    draw_button(s, 153 + 125 - 12, 3, 9, 9, false, "X");
    ksurf_fill(s, KRECT(153, 21, 125, 20), BG_INACTIVE);
    draw_bevel(s, 153, 21, 125, 20, true);

    /* Left edge tile */
    ksurf_fill(s, KRECT(0, 42, 12, 29), BG_DARK);
    ksurf_line(s, 0, 42, 0, 70, BEVEL_LIGHT);

    /* Right edge tile with scrollbar track */
    ksurf_fill(s, KRECT(31, 42, 20, 29), BG_DARK);
    ksurf_line(s, 50, 42, 50, 70, BEVEL_LIGHT);
    ksurf_fill(s, KRECT(40, 42, 8, 29), BG_MID);

    /* Bottom corners */
    ksurf_fill(s, KRECT(0, 72, 125, 38), BG_MID);
    draw_bevel(s, 0, 72, 125, 38, true);
    ksurf_fill(s, KRECT(126, 72, 150, 38), BG_MID);
    draw_bevel(s, 126, 72, 150, 38, true);
    /* Resize handle hatching */
    ksurf_line(s, 258, 100, 268, 90, BEVEL_LIGHT);
    ksurf_line(s, 258, 104, 268, 94, BEVEL_LIGHT);
    ksurf_line(s, 258, 108, 268, 98, BEVEL_LIGHT);

    /* Bottom button sources at y=111 */
    const char *btn_labels[] = {"ADD", "REM", "SEL", "MIS", "LIS"};
    for (int i = 0; i < 5; i++) {
        int x = i * 50;
        draw_button(s, x, 111, 25, 18, false, btn_labels[i]);
        draw_button(s, x + 25, 111, 25, 18, true, btn_labels[i]);
    }
    return save(s, dir, "pledit.bmp");
}

/* pledit.txt - playlist color configuration. */
static bool gen_pledit_txt(const char *dir)
{
    char *path = ka_path_join(dir, "pledit.txt");
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return false;
    fprintf(f,
            "[Text]\n"
            "normal=%s\ncurrent=%s\nnormalbg=%s\nselectedbg=%s\nfont=%s\n",
            DEFAULT_PL_NORMAL, DEFAULT_PL_CURRENT, DEFAULT_PL_NORMALBG,
            DEFAULT_PL_SELECTEDBG, DEFAULT_PL_FONT);
    fclose(f);
    return true;
}

/* viscolor.txt - 24-color visualization palette. */
static bool gen_viscolor_txt(const char *dir)
{
    char *path = ka_path_join(dir, "viscolor.txt");
    FILE *f = fopen(path, "w");
    free(path);
    if (!f)
        return false;
    for (int i = 0; i < VISCOLOR_COUNT; i++)
        fprintf(f, "%d,%d,%d\n", DEFAULT_VISCOLORS[i].r,
                DEFAULT_VISCOLORS[i].g, DEFAULT_VISCOLORS[i].b);
    fclose(f);
    return true;
}

bool skin_default_generate(const char *skin_dir)
{
    if (!ka_mkdirs(skin_dir))
        return false;
    return gen_main(skin_dir) && gen_titlebar(skin_dir) &&
           gen_cbuttons(skin_dir) && gen_posbar(skin_dir) &&
           gen_volume(skin_dir) && gen_balance(skin_dir) &&
           gen_shufrep(skin_dir) && gen_nums_ex(skin_dir) &&
           gen_text(skin_dir) && gen_playpaus(skin_dir) &&
           gen_monoster(skin_dir) && gen_eqmain(skin_dir) &&
           gen_eq_ex(skin_dir) && gen_pledit(skin_dir) &&
           gen_pledit_txt(skin_dir) && gen_viscolor_txt(skin_dir);
}
