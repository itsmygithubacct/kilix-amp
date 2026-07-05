#include "text.h"

#include <ctype.h>
#include <string.h>

#include "consts.h"
#include "render.h"

void text_renderer_init(TextRenderer *tr, SDL_Surface *font_bitmap)
{
    tr->font = font_bitmap;
}

bool text_char_pos(char ch, int *sx, int *sy)
{
    char up = (char)toupper((unsigned char)ch);
    const char *rows[3] = {FONT_CHARS_ROW0, FONT_CHARS_ROW1, FONT_CHARS_ROW2};
    for (int r = 0; r < 3; r++) {
        const char *hit = strchr(rows[r], up);
        if (hit && up) {
            *sx = (int)(hit - rows[r]) * FONT_CHAR_W;
            *sy = r * FONT_CHAR_H;
            return true;
        }
    }
    return false; /* space or unmapped: blank cell */
}

void text_render(TextRenderer *tr, SDL_Surface *dst, const char *text,
                 int x, int y, int max_w)
{
    if (!tr->font)
        return;
    int cx = x;
    for (const char *p = text; *p; p++) {
        if (max_w > 0 && cx - x >= max_w)
            break;
        int sx, sy;
        if (text_char_pos(*p, &sx, &sy))
            ksurf_blit(dst, cx, y, tr->font,
                       KRECT(sx, sy, FONT_CHAR_W, FONT_CHAR_H));
        cx += FONT_CHAR_W;
    }
}

void text_render_scrolling(TextRenderer *tr, SDL_Surface *dst,
                           const char *text, int x, int y, int width,
                           int scroll_offset)
{
    if (!tr->font)
        return;

    char full[1024];
    snprintf(full, sizeof(full), "%s%s", text, SCROLL_SPACER);
    int len = (int)strlen(full);
    int full_w = len * FONT_CHAR_W;
    if (full_w == 0)
        return;

    int offset = scroll_offset % full_w;
    if (offset < 0)
        offset += full_w;

    /* Clip to the display area. */
    SDL_Rect old_clip;
    SDL_GetClipRect(dst, &old_clip);
    SDL_Rect clip = {x, y, width, FONT_CHAR_H};
    SDL_SetClipRect(dst, &clip);

    int start_char = offset / FONT_CHAR_W;
    int pixel_offset = offset % FONT_CHAR_W;

    int cx = x - pixel_offset;
    int idx = start_char;
    while (cx < x + width) {
        char ch = full[idx % len];
        int sx, sy;
        if (text_char_pos(ch, &sx, &sy))
            ksurf_blit(dst, cx, y, tr->font,
                       KRECT(sx, sy, FONT_CHAR_W, FONT_CHAR_H));
        cx += FONT_CHAR_W;
        idx++;
    }

    SDL_SetClipRect(dst, &old_clip);
}

int text_width(const char *text)
{
    return (int)strlen(text) * FONT_CHAR_W;
}

/* --- LCD display --- */

void lcd_init(LCDDisplay *lcd, SDL_Surface *digits_bitmap)
{
    lcd->digits = digits_bitmap;
    lcd->show_remaining = false;
}

void lcd_toggle_mode(LCDDisplay *lcd)
{
    lcd->show_remaining = !lcd->show_remaining;
}

static void draw_digit(LCDDisplay *lcd, SDL_Surface *dst, char ch, int x,
                       int y)
{
    const char *hit = strchr(LCD_DIGITS, ch);
    if (!hit || !ch)
        return;
    int sx = (int)(hit - LCD_DIGITS) * LCD_DIGIT_W;
    ksurf_blit(dst, x, y, lcd->digits,
               KRECT(sx, 0, LCD_DIGIT_W, LCD_DIGIT_H));
}

void lcd_render_time(LCDDisplay *lcd, SDL_Surface *dst, int seconds,
                     int total_seconds, int x, int y)
{
    if (!lcd->digits)
        return;

    int display_secs = seconds;
    bool show_minus = false;
    if (lcd->show_remaining && total_seconds > 0) {
        display_secs = KA_MAX(0, total_seconds - seconds);
        show_minus = true;
    }

    int minutes = KA_MIN(display_secs / 60, 99);
    int secs = display_secs % 60;

    if (show_minus)
        draw_digit(lcd, dst, '-', x - 12, y);

    /* Tens of minutes (blank if 0) */
    draw_digit(lcd, dst, minutes >= 10 ? (char)('0' + minutes / 10) : ' ',
               x, y);
    draw_digit(lcd, dst, (char)('0' + minutes % 10), x + 12, y);

    /* Colon: nums_ex has no colon glyph, so draw two dots. */
    KColor green = {0, 198, 0};
    ksurf_pixel(dst, x + 24 + 1, y + 4, green);
    ksurf_pixel(dst, x + 24 + 1, y + 9, green);

    draw_digit(lcd, dst, (char)('0' + secs / 10), x + 30, y);
    draw_digit(lcd, dst, (char)('0' + secs % 10), x + 42, y);
}
