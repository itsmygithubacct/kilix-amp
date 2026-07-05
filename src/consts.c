#include "consts.h"

const int EQ_BAND_X[10] = {78, 96, 114, 132, 150, 168, 186, 204, 222, 240};

const float SCALE_PRESETS[SCALE_PRESET_COUNT] = {1.0f, 1.5f, 2.0f,
                                                 2.5f, 3.0f, 4.0f};

/* Row 1 index 10 is the ellipsis glyph; stored as '\x85' sentinel and matched
 * by the text renderer (UTF-8 input is folded to it before lookup). */
const char *FONT_CHARS_ROW0 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@";
const char *FONT_CHARS_ROW1 = "0123456789\x85.:()-'!_+\\/[]^&%,=$#";
const char *FONT_CHARS_ROW2 = "?*";

const KColor DEFAULT_VISCOLORS[VISCOLOR_COUNT] = {
    {0, 0, 0},       /* 0: background */
    {24, 33, 41},    /* 1: grid/dots */
    {239, 49, 16},   /* 2: spectrum bar bottom (low) */
    {206, 41, 16},   /* 3 */
    {214, 90, 0},    /* 4 */
    {214, 102, 0},   /* 5 */
    {214, 115, 0},   /* 6 */
    {198, 123, 8},   /* 7 */
    {222, 165, 24},  /* 8 */
    {214, 181, 33},  /* 9 */
    {189, 222, 41},  /* 10 */
    {148, 222, 33},  /* 11 */
    {41, 206, 16},   /* 12 */
    {50, 190, 16},   /* 13 */
    {57, 181, 16},   /* 14 */
    {49, 156, 8},    /* 15 */
    {41, 148, 0},    /* 16: spectrum bar top (high) */
    {24, 132, 8},    /* 17: spectrum bar top overflow */
    {255, 255, 255}, /* 18: oscilloscope color 1 */
    {214, 214, 222}, /* 19: oscilloscope color 2 */
    {181, 189, 189}, /* 20: oscilloscope color 3 */
    {160, 170, 175}, /* 21: oscilloscope color 4 */
    {148, 156, 165}, /* 22: oscilloscope color 5 */
    {150, 150, 150}, /* 23: peak dots */
};

const char *DEFAULT_PL_NORMAL = "#00FF00";
const char *DEFAULT_PL_CURRENT = "#FFFFFF";
const char *DEFAULT_PL_NORMALBG = "#000000";
const char *DEFAULT_PL_SELECTEDBG = "#0000FF";
const char *DEFAULT_PL_FONT = "Arial";

const char *SKIN_BITMAPS[SKIN_BITMAP_COUNT] = {
    "main.bmp",     "titlebar.bmp", "cbuttons.bmp", "posbar.bmp",
    "volume.bmp",   "balance.bmp",  "shufrep.bmp",  "nums_ex.bmp",
    "text.bmp",     "playpaus.bmp", "monoster.bmp", "eqmain.bmp",
    "eq_ex.bmp",    "pledit.bmp",
};

const char *SKIN_CONFIGS[SKIN_CONFIG_COUNT] = {
    "pledit.txt",
    "viscolor.txt",
};

const EQPreset EQ_PRESETS[EQ_PRESET_COUNT] = {
    {"Flat",        {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
    {"Classical",   {0, 0, 0, 0, 0, 0, -4, -4, -4, -6}},
    {"Club",        {0, 0, 2, 4, 4, 4, 2, 0, 0, 0}},
    {"Dance",       {6, 4, 1, 0, 0, -4, -4, -3, 0, 0}},
    {"Full Bass",   {6, 6, 6, 4, 0, -2, -4, -6, -6, -6}},
    {"Full Treble", {-6, -6, -6, -2, 1, 6, 8, 8, 8, 8}},
    {"Headphones",  {3, 6, 3, -2, -1, 1, 3, 6, 8, 8}},
    {"Large Hall",  {6, 6, 4, 4, 0, -3, -3, -3, 0, 0}},
    {"Live",        {-3, 0, 2, 3, 4, 4, 2, 1, 1, 1}},
    {"Pop",         {-1, 3, 4, 5, 3, 0, -1, -1, -1, -1}},
    {"Rock",        {5, 3, -3, -5, -2, 2, 5, 7, 7, 7}},
    {"Soft",        {3, 1, 0, -1, 0, 2, 5, 6, 7, 8}},
    {"Techno",      {5, 4, 0, -4, -3, 0, 5, 6, 6, 5}},
};
