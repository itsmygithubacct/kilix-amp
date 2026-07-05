#include "widgets.h"

#include <math.h>
#include <string.h>

SkinButton skin_button(KRect region, KRect normal_src, KRect pressed_src,
                       const char *bitmap_name, ButtonFn cb, void *ud)
{
    SkinButton b = {0};
    b.region = region;
    b.normal_src = normal_src;
    b.pressed_src = pressed_src;
    b.bitmap_name = bitmap_name;
    b.enabled = true;
    b.callback = cb;
    b.ud = ud;
    return b;
}

KRect skin_button_src(const SkinButton *b)
{
    return b->pressed ? b->pressed_src : b->normal_src;
}

void skin_button_release(SkinButton *b, bool inside)
{
    bool was_pressed = b->pressed;
    b->pressed = false;
    if (was_pressed && inside && b->callback && b->enabled)
        b->callback(b->ud);
}

SkinToggle skin_toggle(KRect region, KRect on_n, KRect on_p, KRect off_n,
                       KRect off_p, const char *bitmap_name, bool state,
                       ToggleFn cb, void *ud)
{
    SkinToggle t = {0};
    t.region = region;
    t.on_normal_src = on_n;
    t.on_pressed_src = on_p;
    t.off_normal_src = off_n;
    t.off_pressed_src = off_p;
    t.bitmap_name = bitmap_name;
    t.state = state;
    t.enabled = true;
    t.callback = cb;
    t.ud = ud;
    return t;
}

KRect skin_toggle_src(const SkinToggle *t)
{
    if (t->state)
        return t->pressed ? t->on_pressed_src : t->on_normal_src;
    return t->pressed ? t->off_pressed_src : t->off_normal_src;
}

void skin_toggle_release(SkinToggle *t, bool inside)
{
    bool was_pressed = t->pressed;
    t->pressed = false;
    if (was_pressed && inside) {
        t->state = !t->state;
        if (t->callback && t->enabled)
            t->callback(t->ud, t->state);
    }
}

SkinSlider skin_slider(KRect region, KRect thumb_n, KRect thumb_p,
                       int thumb_w, int thumb_h, const char *bitmap_name,
                       SliderOrientation orient, int min_val, int max_val,
                       int value, SliderFn cb, void *ud)
{
    SkinSlider s = {0};
    s.region = region;
    s.thumb_normal_src = thumb_n;
    s.thumb_pressed_src = thumb_p;
    s.thumb_w = thumb_w;
    s.thumb_h = thumb_h;
    s.bitmap_name = bitmap_name;
    s.orientation = orient;
    s.min_val = min_val;
    s.max_val = max_val;
    s.value = value;
    s.enabled = true;
    s.callback = cb;
    s.ud = ud;
    return s;
}

void skin_slider_set_value(SkinSlider *s, int v)
{
    s->value = KA_CLAMP(v, s->min_val, s->max_val);
}

int skin_slider_thumb_pos(const SkinSlider *s)
{
    int track_len = (s->orientation == SLIDER_HORIZONTAL)
                        ? s->region.w - s->thumb_w
                        : s->region.h - s->thumb_h;
    if (s->max_val == s->min_val)
        return 0;
    double ratio =
        (double)(s->value - s->min_val) / (s->max_val - s->min_val);
    if (s->orientation == SLIDER_VERTICAL)
        ratio = 1.0 - ratio; /* vertical sliders: top = max */
    return (int)(ratio * track_len);
}

KRect skin_slider_thumb_rect(const SkinSlider *s)
{
    int pos = skin_slider_thumb_pos(s);
    if (s->orientation == SLIDER_HORIZONTAL)
        return KRECT(s->region.x + pos, s->region.y, s->thumb_w, s->thumb_h);
    return KRECT(s->region.x, s->region.y + pos, s->thumb_w, s->thumb_h);
}

KRect skin_slider_thumb_src(const SkinSlider *s)
{
    return s->pressed ? s->thumb_pressed_src : s->thumb_normal_src;
}

void skin_slider_press(SkinSlider *s, int px, int py)
{
    s->pressed = true;
    KRect t = skin_slider_thumb_rect(s);
    if (krect_contains(t, px, py)) {
        /* Clicked the thumb: keep the grab offset so the drag is relative. */
        s->drag_offset = (s->orientation == SLIDER_HORIZONTAL) ? px - t.x
                                                               : py - t.y;
    } else {
        /* Clicked the bare track: jump the thumb to the click position and
         * fire the callback for an immediate single-click seek. */
        s->drag_offset = 0;
        skin_slider_drag(s, px, py);
    }
}

void skin_slider_drag(SkinSlider *s, int px, int py)
{
    if (!s->pressed)
        return;
    int track_len;
    double ratio;
    if (s->orientation == SLIDER_HORIZONTAL) {
        track_len = s->region.w - s->thumb_w;
        if (track_len <= 0)
            return;
        int pos = KA_CLAMP(px - s->region.x - s->drag_offset, 0, track_len);
        ratio = (double)pos / track_len;
    } else {
        track_len = s->region.h - s->thumb_h;
        if (track_len <= 0)
            return;
        int pos = KA_CLAMP(py - s->region.y - s->drag_offset, 0, track_len);
        ratio = 1.0 - (double)pos / track_len; /* vertical: top = max */
    }
    int v = (int)lround(s->min_val + ratio * (s->max_val - s->min_val));
    s->value = KA_CLAMP(v, s->min_val, s->max_val);
    if (s->callback && s->enabled)
        s->callback(s->ud, s->value);
}

void skin_slider_release(SkinSlider *s)
{
    s->pressed = false;
}

/* --- HitMap --- */

void hitmap_init(HitMap *hm)
{
    memset(hm, 0, sizeof(*hm));
}

void hitmap_add_button(HitMap *hm, SkinButton *b)
{
    if (hm->n_buttons < HITMAP_MAX)
        hm->buttons[hm->n_buttons++] = b;
}

void hitmap_add_toggle(HitMap *hm, SkinToggle *t)
{
    if (hm->n_toggles < HITMAP_MAX)
        hm->toggles[hm->n_toggles++] = t;
}

void hitmap_add_slider(HitMap *hm, SkinSlider *s)
{
    if (hm->n_sliders < HITMAP_MAX)
        hm->sliders[hm->n_sliders++] = s;
}

void hitmap_add_region(HitMap *hm, KRect rect, const char *name)
{
    if (hm->n_regions < HITMAP_MAX)
        hm->regions[hm->n_regions++] = (HitRegion){rect, name};
}

HitKind hitmap_mouse_press(HitMap *hm, int x, int y,
                           const char **region_name)
{
    if (region_name)
        *region_name = NULL;
    for (int i = 0; i < hm->n_sliders; i++) {
        SkinSlider *s = hm->sliders[i];
        if (krect_contains(s->region, x, y) && s->enabled) {
            skin_slider_press(s, x, y);
            hm->captured = s;
            hm->captured_kind = HIT_SLIDER;
            return HIT_SLIDER;
        }
    }
    for (int i = 0; i < hm->n_buttons; i++) {
        SkinButton *b = hm->buttons[i];
        if (krect_contains(b->region, x, y) && b->enabled) {
            b->pressed = true;
            hm->captured = b;
            hm->captured_kind = HIT_BUTTON;
            return HIT_BUTTON;
        }
    }
    for (int i = 0; i < hm->n_toggles; i++) {
        SkinToggle *t = hm->toggles[i];
        if (krect_contains(t->region, x, y) && t->enabled) {
            t->pressed = true;
            hm->captured = t;
            hm->captured_kind = HIT_TOGGLE;
            return HIT_TOGGLE;
        }
    }
    for (int i = 0; i < hm->n_regions; i++) {
        if (krect_contains(hm->regions[i].rect, x, y)) {
            if (region_name)
                *region_name = hm->regions[i].name;
            return HIT_REGION;
        }
    }
    return HIT_NONE;
}

void hitmap_mouse_move(HitMap *hm, int x, int y)
{
    switch (hm->captured_kind) {
    case HIT_SLIDER:
        skin_slider_drag(hm->captured, x, y);
        break;
    case HIT_BUTTON: {
        SkinButton *b = hm->captured;
        b->pressed = krect_contains(b->region, x, y);
        break;
    }
    case HIT_TOGGLE: {
        SkinToggle *t = hm->captured;
        t->pressed = krect_contains(t->region, x, y);
        break;
    }
    default:
        break;
    }
}

void hitmap_mouse_release(HitMap *hm, int x, int y)
{
    switch (hm->captured_kind) {
    case HIT_SLIDER:
        skin_slider_release(hm->captured);
        break;
    case HIT_BUTTON: {
        SkinButton *b = hm->captured;
        skin_button_release(b, krect_contains(b->region, x, y));
        break;
    }
    case HIT_TOGGLE: {
        SkinToggle *t = hm->captured;
        skin_toggle_release(t, krect_contains(t->region, x, y));
        break;
    }
    default:
        break;
    }
    hm->captured = NULL;
    hm->captured_kind = HIT_NONE;
}

bool hitmap_is_captured(const HitMap *hm)
{
    return hm->captured != NULL;
}

void hitmap_clear(HitMap *hm)
{
    hitmap_init(hm);
}
