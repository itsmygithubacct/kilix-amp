/* Widget primitive tests: regions, buttons, toggles, sliders, hitmap.
 * Ports tests/test_widgets.py. */
#include "ktest.h"

#include "widgets.h"

static int g_button_fires;
static void on_button(void *ud)
{
    (void)ud;
    g_button_fires++;
}

static bool g_toggle_state;
static int g_toggle_fires;
static void on_toggle(void *ud, bool state)
{
    (void)ud;
    g_toggle_state = state;
    g_toggle_fires++;
}

static int g_slider_value = -1;
static void on_slider(void *ud, int v)
{
    (void)ud;
    g_slider_value = v;
}

static void test_region_contains(void)
{
    KRect r = KRECT(10, 20, 30, 40);
    ASSERT_TRUE(krect_contains(r, 10, 20));
    ASSERT_TRUE(krect_contains(r, 39, 59));
    ASSERT_FALSE(krect_contains(r, 40, 20)); /* right edge exclusive */
    ASSERT_FALSE(krect_contains(r, 10, 60));
    ASSERT_FALSE(krect_contains(r, 9, 20));
}

static void test_button_press_release(void)
{
    g_button_fires = 0;
    SkinButton b = skin_button(KRECT(0, 0, 10, 10), KRECT(0, 0, 10, 10),
                               KRECT(10, 0, 10, 10), "cbuttons", on_button,
                               NULL);
    /* Normal src when unpressed, pressed src when pressed */
    ASSERT_EQ_INT(skin_button_src(&b).x, 0);
    b.pressed = true;
    ASSERT_EQ_INT(skin_button_src(&b).x, 10);
    /* Release inside fires */
    skin_button_release(&b, true);
    ASSERT_EQ_INT(g_button_fires, 1);
    ASSERT_FALSE(b.pressed);
    /* Release outside does not fire */
    b.pressed = true;
    skin_button_release(&b, false);
    ASSERT_EQ_INT(g_button_fires, 1);
}

static void test_toggle(void)
{
    g_toggle_fires = 0;
    SkinToggle t = skin_toggle(KRECT(0, 0, 10, 10), KRECT(0, 0, 1, 1),
                               KRECT(1, 0, 1, 1), KRECT(2, 0, 1, 1),
                               KRECT(3, 0, 1, 1), "shufrep", false,
                               on_toggle, NULL);
    ASSERT_EQ_INT(skin_toggle_src(&t).x, 2); /* off normal */
    t.pressed = true;
    ASSERT_EQ_INT(skin_toggle_src(&t).x, 3); /* off pressed */
    skin_toggle_release(&t, true);
    ASSERT_TRUE(t.state);
    ASSERT_TRUE(g_toggle_state);
    ASSERT_EQ_INT(g_toggle_fires, 1);
    ASSERT_EQ_INT(skin_toggle_src(&t).x, 0); /* on normal */
    /* Release outside cancels without toggling */
    t.pressed = true;
    skin_toggle_release(&t, false);
    ASSERT_TRUE(t.state);
    ASSERT_EQ_INT(g_toggle_fires, 1);
}

static void test_slider_value_bounds(void)
{
    SkinSlider s = skin_slider(KRECT(0, 0, 100, 10), KRECT(0, 0, 1, 1),
                               KRECT(0, 0, 1, 1), 10, 10, "volume",
                               SLIDER_HORIZONTAL, 0, 100, 50, NULL, NULL);
    skin_slider_set_value(&s, 150);
    ASSERT_EQ_INT(s.value, 100);
    skin_slider_set_value(&s, -10);
    ASSERT_EQ_INT(s.value, 0);
}

static void test_slider_thumb_pos(void)
{
    SkinSlider s = skin_slider(KRECT(0, 0, 110, 10), KRECT(0, 0, 1, 1),
                               KRECT(0, 0, 1, 1), 10, 10, "volume",
                               SLIDER_HORIZONTAL, 0, 100, 0, NULL, NULL);
    ASSERT_EQ_INT(skin_slider_thumb_pos(&s), 0);
    skin_slider_set_value(&s, 100);
    ASSERT_EQ_INT(skin_slider_thumb_pos(&s), 100); /* track = 110-10 */
    skin_slider_set_value(&s, 50);
    ASSERT_EQ_INT(skin_slider_thumb_pos(&s), 50);
}

static void test_vertical_slider_inverted(void)
{
    SkinSlider s = skin_slider(KRECT(0, 0, 14, 63), KRECT(0, 0, 1, 1),
                               KRECT(0, 0, 1, 1), 14, 11, "eqmain",
                               SLIDER_VERTICAL, -12, 12, 12, NULL, NULL);
    /* Max value = top */
    ASSERT_EQ_INT(skin_slider_thumb_pos(&s), 0);
    skin_slider_set_value(&s, -12);
    ASSERT_EQ_INT(skin_slider_thumb_pos(&s), 52);
}

static void test_slider_drag_callback(void)
{
    g_slider_value = -1;
    SkinSlider s = skin_slider(KRECT(0, 0, 110, 10), KRECT(0, 0, 1, 1),
                               KRECT(0, 0, 1, 1), 10, 10, "volume",
                               SLIDER_HORIZONTAL, 0, 100, 0, on_slider,
                               NULL);
    /* Track click jumps and fires immediately: pos 55 / track 100 */
    skin_slider_press(&s, 55, 5);
    ASSERT_TRUE(s.pressed);
    ASSERT_EQ_INT(g_slider_value, 55);
    skin_slider_drag(&s, 100, 5);
    ASSERT_EQ_INT(g_slider_value, 100);
    skin_slider_release(&s);
    ASSERT_FALSE(s.pressed);
}

static void test_hitmap_dispatch(void)
{
    g_button_fires = 0;
    g_slider_value = -1;
    HitMap hm;
    hitmap_init(&hm);
    SkinButton b = skin_button(KRECT(0, 0, 10, 10), KRECT(0, 0, 1, 1),
                               KRECT(0, 0, 1, 1), "cbuttons", on_button,
                               NULL);
    SkinSlider s = skin_slider(KRECT(20, 0, 110, 10), KRECT(0, 0, 1, 1),
                               KRECT(0, 0, 1, 1), 10, 10, "volume",
                               SLIDER_HORIZONTAL, 0, 100, 0, on_slider,
                               NULL);
    hitmap_add_button(&hm, &b);
    hitmap_add_slider(&hm, &s);
    hitmap_add_region(&hm, KRECT(50, 50, 10, 10), "special");

    /* Button hit */
    ASSERT_EQ_INT(hitmap_mouse_press(&hm, 5, 5, NULL), HIT_BUTTON);
    ASSERT_TRUE(hitmap_is_captured(&hm));
    hitmap_mouse_release(&hm, 5, 5);
    ASSERT_EQ_INT(g_button_fires, 1);
    ASSERT_FALSE(hitmap_is_captured(&hm));

    /* Slider takes priority in its region and captures */
    ASSERT_EQ_INT(hitmap_mouse_press(&hm, 30, 5, NULL), HIT_SLIDER);
    hitmap_mouse_move(&hm, 130, 5);
    ASSERT_EQ_INT(g_slider_value, 100);
    hitmap_mouse_release(&hm, 130, 5);

    /* Named region */
    const char *name = NULL;
    ASSERT_EQ_INT(hitmap_mouse_press(&hm, 55, 55, &name), HIT_REGION);
    ASSERT_STR_EQ(name, "special");

    /* Miss */
    ASSERT_EQ_INT(hitmap_mouse_press(&hm, 200, 200, NULL), HIT_NONE);
}

int main(void)
{
    RUN(test_region_contains);
    RUN(test_button_press_release);
    RUN(test_toggle);
    RUN(test_slider_value_bounds);
    RUN(test_slider_thumb_pos);
    RUN(test_vertical_slider_inverted);
    RUN(test_slider_drag_callback);
    RUN(test_hitmap_dispatch);
    return kt_summary("test_widgets");
}
