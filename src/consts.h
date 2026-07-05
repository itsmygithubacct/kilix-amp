/* Pixel coordinates, dimensions, and colors from the Winamp 2.x skin spec.
 * Direct port of nixamp lib/constants.py. */
#ifndef KA_CONSTS_H
#define KA_CONSTS_H

#include "common.h"

/* ==== Main Window (275x116) ============================================= */
#define MAIN_W 275
#define MAIN_H 116

#define TITLE_BAR              KRECT(0, 0, 275, 14)
#define TITLE_BAR_ACTIVE_SRC   KRECT(27, 0, 275, 14) /* from titlebar.bmp */
#define TITLE_BAR_INACTIVE_SRC KRECT(27, 15, 275, 14)

/* Title bar buttons (from titlebar.bmp) */
#define MENU_BTN             KRECT(6, 3, 9, 9)
#define MINIMIZE_BTN         KRECT(244, 3, 9, 9)
#define MINIMIZE_SRC_NORMAL  KRECT(0, 18, 9, 9)
#define MINIMIZE_SRC_PRESSED KRECT(9, 18, 9, 9)
#define SHADE_BTN            KRECT(254, 3, 9, 9)
#define SHADE_SRC_NORMAL     KRECT(0, 18, 9, 9)
#define SHADE_SRC_PRESSED    KRECT(9, 18, 9, 9)
#define CLOSE_BTN            KRECT(264, 3, 9, 9)
#define CLOSE_SRC_NORMAL     KRECT(18, 0, 9, 9)
#define CLOSE_SRC_PRESSED    KRECT(18, 9, 9, 9)

#define CLUTTERBAR KRECT(10, 22, 8, 43)
#define VIS_AREA   KRECT(24, 43, 76, 16)

/* Song title display (scrolling text) */
#define TITLE_DISPLAY KRECT(111, 24, 154, 6)

/* Time display (LCD digits area) */
#define TIME_DISPLAY KRECT(48, 26, 63, 13)
#define TIME_MINUS   KRECT(36, 26, 9, 1)
#define TIME_DIGIT_1 KRECT(48, 26, 9, 13)
#define TIME_DIGIT_2 KRECT(60, 26, 9, 13)
#define TIME_DIGIT_3 KRECT(78, 26, 9, 13)
#define TIME_DIGIT_4 KRECT(90, 26, 9, 13)
#define TIME_COLON   KRECT(72, 26, 5, 13)

#define KBPS_DISPLAY KRECT(111, 43, 15, 6)
#define KHZ_DISPLAY  KRECT(156, 43, 10, 6)

/* Mono/Stereo indicators (sources in monoster.bmp) */
#define MONO_INDICATOR   KRECT(212, 41, 27, 12)
#define STEREO_INDICATOR KRECT(239, 41, 29, 12)
#define MONO_ON_SRC      KRECT(29, 0, 27, 12)
#define MONO_OFF_SRC     KRECT(29, 12, 27, 12)
#define STEREO_ON_SRC    KRECT(0, 0, 29, 12)
#define STEREO_OFF_SRC   KRECT(0, 12, 29, 12)

/* Play status indicator (from playpaus.bmp, each icon 9x9) */
#define PLAYPAUS_AREA  KRECT(24, 28, 11, 9)
#define PLAYPAUS_PLAY  KRECT(0, 0, 9, 9)
#define PLAYPAUS_PAUSE KRECT(9, 0, 9, 9)
#define PLAYPAUS_STOP  KRECT(18, 0, 9, 9)

/* Position slider (seek bar) */
#define POS_BAR            KRECT(16, 72, 248, 10)
#define POS_BAR_BG_SRC     KRECT(0, 0, 248, 10) /* from posbar.bmp */
#define POS_THUMB_NORMAL   KRECT(248, 0, 29, 10)
#define POS_THUMB_PRESSED  KRECT(278, 0, 29, 10)

/* Transport buttons (from cbuttons.bmp), each 23x18 */
#define CBUTTONS_Y 88
#define BTN_PREV  KRECT(16, CBUTTONS_Y, 23, 18)
#define BTN_PLAY  KRECT(39, CBUTTONS_Y, 23, 18)
#define BTN_PAUSE KRECT(62, CBUTTONS_Y, 23, 18)
#define BTN_STOP  KRECT(85, CBUTTONS_Y, 23, 18)
#define BTN_NEXT  KRECT(108, CBUTTONS_Y, 22, 18)
#define BTN_EJECT KRECT(136, CBUTTONS_Y, 22, 16)

/* cbuttons.bmp source regions (normal then pressed) */
#define CBUTTONS_PREV_N  KRECT(0, 0, 23, 18)
#define CBUTTONS_PREV_P  KRECT(0, 18, 23, 18)
#define CBUTTONS_PLAY_N  KRECT(23, 0, 23, 18)
#define CBUTTONS_PLAY_P  KRECT(23, 18, 23, 18)
#define CBUTTONS_PAUSE_N KRECT(46, 0, 23, 18)
#define CBUTTONS_PAUSE_P KRECT(46, 18, 23, 18)
#define CBUTTONS_STOP_N  KRECT(69, 0, 23, 18)
#define CBUTTONS_STOP_P  KRECT(69, 18, 23, 18)
#define CBUTTONS_NEXT_N  KRECT(92, 0, 22, 18)
#define CBUTTONS_NEXT_P  KRECT(92, 18, 22, 18)
#define CBUTTONS_EJECT_N KRECT(114, 0, 22, 16)
#define CBUTTONS_EJECT_P KRECT(114, 16, 22, 16)

/* Volume slider: volume.bmp is 28 frames of 68x13 stacked vertically */
#define VOLUME_AREA        KRECT(107, 57, 68, 13)
#define VOLUME_BG_FRAME_W  68
#define VOLUME_BG_FRAME_H  13
#define VOLUME_FRAMES      28
#define VOLUME_THUMB_W     14
#define VOLUME_THUMB_H     11
#define VOLUME_THUMB_NORMAL_SRC  KRECT(15, 422, 14, 11)
#define VOLUME_THUMB_PRESSED_SRC KRECT(0, 422, 14, 11)

/* Balance slider: balance.bmp is 28 frames of 38x13 stacked vertically */
#define BALANCE_AREA        KRECT(177, 57, 38, 13)
#define BALANCE_BG_FRAME_W  38
#define BALANCE_BG_FRAME_H  13
#define BALANCE_FRAMES      28
#define BALANCE_THUMB_W     14
#define BALANCE_THUMB_H     11
#define BALANCE_THUMB_NORMAL_SRC  KRECT(15, 422, 14, 11)
#define BALANCE_THUMB_PRESSED_SRC KRECT(0, 422, 14, 11)

/* Shuffle/Repeat/EQ/PL toggles (from shufrep.bmp) */
#define SHUFFLE_BTN KRECT(164, 89, 46, 15)
#define REPEAT_BTN  KRECT(210, 89, 28, 15)
#define EQ_BTN      KRECT(219, 58, 23, 12)
#define PL_BTN      KRECT(242, 58, 23, 12)

#define SHUFFLE_ON_N  KRECT(28, 0, 46, 15)
#define SHUFFLE_ON_P  KRECT(28, 15, 46, 15)
#define SHUFFLE_OFF_N KRECT(28, 30, 46, 15)
#define SHUFFLE_OFF_P KRECT(28, 45, 46, 15)
#define REPEAT_ON_N   KRECT(0, 0, 28, 15)
#define REPEAT_ON_P   KRECT(0, 15, 28, 15)
#define REPEAT_OFF_N  KRECT(0, 30, 28, 15)
#define REPEAT_OFF_P  KRECT(0, 45, 28, 15)
#define EQ_ON_N  KRECT(0, 61, 23, 12)
#define EQ_ON_P  KRECT(46, 61, 23, 12)
#define EQ_OFF_N KRECT(0, 73, 23, 12)
#define EQ_OFF_P KRECT(46, 73, 23, 12)
#define PL_ON_N  KRECT(23, 61, 23, 12)
#define PL_ON_P  KRECT(69, 61, 23, 12)
#define PL_OFF_N KRECT(23, 73, 23, 12)
#define PL_OFF_P KRECT(69, 73, 23, 12)

/* Window shade mode */
#define SHADE_H 14

/* ==== EQ Window (275x116) =============================================== */
#define EQ_W 275
#define EQ_H 116

#define EQ_TITLE_BAR          KRECT(0, 0, 275, 14)
#define EQ_TITLE_ACTIVE_SRC   KRECT(0, 134, 275, 14) /* from eqmain.bmp */
#define EQ_TITLE_INACTIVE_SRC KRECT(0, 149, 275, 14)

#define EQ_CLOSE_BTN KRECT(264, 3, 9, 9)

#define EQ_ON_BTN     KRECT(14, 18, 25, 12)
#define EQ_ON_ON_SRC  KRECT(10, 119, 25, 12)
#define EQ_ON_OFF_SRC KRECT(36, 119, 25, 12)

#define EQ_AUTO_BTN     KRECT(39, 18, 33, 12)
#define EQ_AUTO_ON_SRC  KRECT(61, 119, 33, 12)
#define EQ_AUTO_OFF_SRC KRECT(94, 119, 33, 12)

#define EQ_PRESETS_BTN   KRECT(217, 18, 44, 12)
#define EQ_PRESETS_N_SRC KRECT(224, 164, 44, 12)
#define EQ_PRESETS_P_SRC KRECT(224, 176, 44, 12)

/* EQ graph area (spline curve display) */
#define EQ_GRAPH KRECT(86, 17, 113, 19)

/* EQ sliders: 11 vertical sliders (preamp + 10 bands) */
#define EQ_SLIDER_W      14
#define EQ_SLIDER_H      63
#define EQ_SLIDER_TRAVEL 52
#define EQ_SLIDER_Y      38
#define EQ_PREAMP_X      21
/* Frequencies: 60, 170, 310, 600, 1K, 3K, 6K, 12K, 14K, 16K */
extern const int EQ_BAND_X[10];

#define EQ_SLIDER_THUMB_N KRECT(0, 164, 14, 11)
#define EQ_SLIDER_THUMB_P KRECT(0, 176, 14, 11)
#define EQ_BG_SRC         KRECT(0, 0, 275, 116)

/* ==== Playlist Window (275xN, resizable) ================================ */
#define PL_MIN_W     275
#define PL_MIN_H     116
#define PL_DEFAULT_H 232
#define PL_TITLE_H   20

/* pledit.bmp regions - corner pieces for 9-slice */
#define PL_TL_ACTIVE              KRECT(0, 0, 25, 20)
#define PL_TL_INACTIVE            KRECT(0, 21, 25, 20)
#define PL_TITLE_STRETCH_ACTIVE   KRECT(26, 0, 100, 20)
#define PL_TITLE_STRETCH_INACTIVE KRECT(26, 21, 100, 20)
#define PL_TR_ACTIVE              KRECT(153, 0, 125, 20)
#define PL_TR_INACTIVE            KRECT(153, 21, 125, 20)

#define PL_LEFT_TILE  KRECT(0, 42, 12, 29)
#define PL_RIGHT_TILE KRECT(31, 42, 20, 29)

#define PL_BL_CORNER      KRECT(0, 72, 125, 38)
#define PL_BR_CORNER      KRECT(126, 72, 150, 38)
#define PL_BOTTOM_STRETCH KRECT(179, 0, 25, 38)

#define PL_SCROLLBAR_X_OFFSET (-20) /* from right edge */
#define PL_SCROLLBAR_W 8
#define PL_ITEM_H      13

/* Playlist bottom buttons (from pledit.bmp) */
#define PL_BTN_H 18
#define PL_BTN_ADD  KRECT(0, 111, 25, 18)
#define PL_BTN_REM  KRECT(54, 111, 25, 18)
#define PL_BTN_SEL  KRECT(104, 111, 25, 18)
#define PL_BTN_MISC KRECT(154, 111, 25, 18)
#define PL_BTN_LIST KRECT(204, 111, 25, 18)

#define PL_RESIZE_HANDLE KRECT(-20, -20, 20, 20)

/* ==== Bitmap font (text.bmp) ============================================ */
#define FONT_CHAR_W 5
#define FONT_CHAR_H 6

/* Character map - row 0: A-Z + symbols, row 1: digits + symbols, row 2: ?* .
 * The Python source uses U+2026 (ellipsis) at row1 index 10; we keep the
 * same cell but match it from ASCII input via "..." handling upstream. */
extern const char *FONT_CHARS_ROW0; /* A-Z quote at */
extern const char *FONT_CHARS_ROW1; /* 0-9 ellipsis . : ( ) - ' ! _ + \ / [ ] ^ & % , = $ # */
extern const char *FONT_CHARS_ROW2; /* ? * */

#define SCROLL_SPEED  1
#define SCROLL_FPS    20
#define SCROLL_SPACER " *** "

/* ==== LCD Digits (nums_ex.bmp) ========================================== */
#define LCD_DIGIT_W 9
#define LCD_DIGIT_H 13
#define LCD_DIGITS  "0123456789 -" /* digit order in nums_ex.bmp */

/* ==== Spectrum Analyzer ================================================= */
#define SPEC_BARS   75
#define SPEC_BAR_W  1
#define SPEC_BAR_H  16
#define SPEC_AREA_W 76
#define SPEC_AREA_H 16

#define PEAK_GRAVITY     0.5f
#define PEAK_HOLD_FRAMES 12

/* ==== Default viscolor.txt palette (24 colors) ========================== */
#define VISCOLOR_COUNT 24
extern const KColor DEFAULT_VISCOLORS[VISCOLOR_COUNT];

/* ==== Default pledit.txt colors ========================================= */
extern const char *DEFAULT_PL_NORMAL;     /* #00FF00 */
extern const char *DEFAULT_PL_CURRENT;    /* #FFFFFF */
extern const char *DEFAULT_PL_NORMALBG;   /* #000000 */
extern const char *DEFAULT_PL_SELECTEDBG; /* #0000FF */
extern const char *DEFAULT_PL_FONT;       /* Arial */

/* ==== Skin files (case-insensitive matching) ============================ */
#define SKIN_BITMAP_COUNT 14
extern const char *SKIN_BITMAPS[SKIN_BITMAP_COUNT];
#define SKIN_CONFIG_COUNT 2
extern const char *SKIN_CONFIGS[SKIN_CONFIG_COUNT];

/* ==== EQ Presets ======================================================== */
typedef struct {
    const char *name;
    float gains[10];
} EQPreset;
#define EQ_PRESET_COUNT 13
extern const EQPreset EQ_PRESETS[EQ_PRESET_COUNT];

/* ==== Window Docking ==================================================== */
#define DOCK_DISTANCE 10 /* pixels to snap */

/* ==== Editor Window (275xN, resizable) ================================== */
#define ED_MIN_W       275
#define ED_MIN_H       200
#define ED_DEFAULT_H   300
#define ED_TOOLBAR_H   24
#define ED_RULER_H     16
#define ED_INFO_BAR_H  14
#define ED_TITLE_H     20 /* same as PL_TITLE_H */

/* ==== UI scaling ======================================================== */
#define DOUBLE_SCALE 2     /* classic double-size */
#define SCALE_MIN 1.0f     /* smallest UI scale factor */
#define SCALE_MAX 4.0f     /* largest UI scale factor */
/* Preset scales offered in the SIZE menu / cycled by the D key. */
#define SCALE_PRESET_COUNT 6
extern const float SCALE_PRESETS[SCALE_PRESET_COUNT];

#endif
