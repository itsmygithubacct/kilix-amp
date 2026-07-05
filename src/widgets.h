/* Skin-aware widget primitives: buttons, toggles, sliders, hit regions.
 * Port of nixamp lib/widgets.py. */
#ifndef KA_WIDGETS_H
#define KA_WIDGETS_H

#include "common.h"

typedef void (*ButtonFn)(void *ud);
typedef void (*ToggleFn)(void *ud, bool state);
typedef void (*SliderFn)(void *ud, int value);

static inline bool krect_contains(KRect r, int px, int py)
{
    return r.x <= px && px < r.x + r.w && r.y <= py && py < r.y + r.h;
}

/* Momentary press button with normal/pressed bitmap states. */
typedef struct {
    KRect region;
    KRect normal_src, pressed_src;
    const char *bitmap_name; /* skin sheet, e.g. "cbuttons" */
    bool pressed;
    bool enabled;
    ButtonFn callback;
    void *ud;
} SkinButton;

SkinButton skin_button(KRect region, KRect normal_src, KRect pressed_src,
                       const char *bitmap_name, ButtonFn cb, void *ud);
KRect skin_button_src(const SkinButton *b);
void skin_button_release(SkinButton *b, bool inside);

/* On/off toggle with 4 states (off/on x normal/pressed). */
typedef struct {
    KRect region;
    KRect on_normal_src, on_pressed_src, off_normal_src, off_pressed_src;
    const char *bitmap_name;
    bool state;
    bool pressed;
    bool enabled;
    ToggleFn callback;
    void *ud;
} SkinToggle;

SkinToggle skin_toggle(KRect region, KRect on_n, KRect on_p, KRect off_n,
                       KRect off_p, const char *bitmap_name, bool state,
                       ToggleFn cb, void *ud);
KRect skin_toggle_src(const SkinToggle *t);
void skin_toggle_release(SkinToggle *t, bool inside);

/* Horizontal or vertical slider with thumb tracking. */
typedef enum { SLIDER_HORIZONTAL, SLIDER_VERTICAL } SliderOrientation;

typedef struct {
    KRect region;
    KRect thumb_normal_src, thumb_pressed_src;
    int thumb_w, thumb_h;
    const char *bitmap_name;
    SliderOrientation orientation;
    int min_val, max_val;
    int value;
    bool pressed;
    bool enabled;
    SliderFn callback;
    void *ud;
    int drag_offset;
} SkinSlider;

SkinSlider skin_slider(KRect region, KRect thumb_n, KRect thumb_p,
                       int thumb_w, int thumb_h, const char *bitmap_name,
                       SliderOrientation orient, int min_val, int max_val,
                       int value, SliderFn cb, void *ud);
void skin_slider_set_value(SkinSlider *s, int v); /* clamps, no callback */
int skin_slider_thumb_pos(const SkinSlider *s);   /* pixels along track */
KRect skin_slider_thumb_rect(const SkinSlider *s);
KRect skin_slider_thumb_src(const SkinSlider *s);
void skin_slider_press(SkinSlider *s, int px, int py);
void skin_slider_drag(SkinSlider *s, int px, int py);
void skin_slider_release(SkinSlider *s);

/* Priority-based mouse dispatch: sliders > buttons > toggles > regions. */
typedef enum {
    HIT_NONE = 0,
    HIT_BUTTON,
    HIT_TOGGLE,
    HIT_SLIDER,
    HIT_REGION,
} HitKind;

typedef struct {
    KRect rect;
    const char *name;
} HitRegion;

#define HITMAP_MAX 64

typedef struct {
    SkinButton *buttons[HITMAP_MAX];
    int n_buttons;
    SkinToggle *toggles[HITMAP_MAX];
    int n_toggles;
    SkinSlider *sliders[HITMAP_MAX];
    int n_sliders;
    HitRegion regions[HITMAP_MAX];
    int n_regions;
    HitKind captured_kind;
    void *captured;
} HitMap;

void hitmap_init(HitMap *hm);
void hitmap_add_button(HitMap *hm, SkinButton *b);
void hitmap_add_toggle(HitMap *hm, SkinToggle *t);
void hitmap_add_slider(HitMap *hm, SkinSlider *s);
void hitmap_add_region(HitMap *hm, KRect rect, const char *name);
/* Returns what was hit; for HIT_REGION, *region_name is set. */
HitKind hitmap_mouse_press(HitMap *hm, int x, int y,
                           const char **region_name);
void hitmap_mouse_move(HitMap *hm, int x, int y);
void hitmap_mouse_release(HitMap *hm, int x, int y);
bool hitmap_is_captured(const HitMap *hm);
void hitmap_clear(HitMap *hm);

#endif
