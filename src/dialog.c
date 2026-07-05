#include "dialog.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font4x6.h"
#include "render.h"

#define DLG_W 260
#define ROW_H 18
#define FIELD_X 110

#define DLG_BG      ((KColor){40, 40, 45})
#define DLG_BORDER  ((KColor){85, 88, 95})
#define DLG_TEXT    ((KColor){200, 200, 200})
#define DLG_TITLE   ((KColor){0, 198, 0})
#define DLG_FIELDBG ((KColor){20, 20, 24})
#define DLG_FOCUSBG ((KColor){30, 40, 30})
#define DLG_BTN     ((KColor){58, 60, 66})

DialogField dialog_field_text(const char *name, const char *label,
                              const char *initial)
{
    DialogField f = {0};
    f.name = name;
    f.label = label;
    snprintf(f.value, sizeof(f.value), "%s", initial ? initial : "");
    return f;
}

DialogField dialog_field_num(const char *name, const char *label,
                             double initial)
{
    DialogField f = {0};
    f.name = name;
    f.label = label;
    snprintf(f.value, sizeof(f.value), "%g", initial);
    return f;
}

DialogField dialog_field_combo(const char *name, const char *label,
                               const char **options)
{
    DialogField f = {0};
    f.name = name;
    f.label = label;
    f.options = options;
    f.option_idx = 0;
    snprintf(f.value, sizeof(f.value), "%s", options[0]);
    return f;
}

static void render_dialog(SDL_Surface *s, const char *title,
                          DialogField *fields, int n, int focus)
{
    ksurf_fill(s, KRECT(0, 0, s->w, s->h), DLG_BG);
    ksurf_line(s, 0, 0, s->w - 1, 0, DLG_BORDER);
    ksurf_line(s, 0, 0, 0, s->h - 1, DLG_BORDER);
    ksurf_line(s, s->w - 1, 0, s->w - 1, s->h - 1, DLG_BORDER);
    ksurf_line(s, 0, s->h - 1, s->w - 1, s->h - 1, DLG_BORDER);

    font4x6_draw(s, 8, 6, title, DLG_TITLE);

    for (int i = 0; i < n; i++) {
        int y = 20 + i * ROW_H;
        font4x6_draw(s, 8, y + 5, fields[i].label, DLG_TEXT);
        KRect box = KRECT(FIELD_X, y + 1, s->w - FIELD_X - 8, ROW_H - 4);
        ksurf_fill(s, box, i == focus ? DLG_FOCUSBG : DLG_FIELDBG);
        char shown[DIALOG_VALUE_LEN + 2];
        if (fields[i].options)
            snprintf(shown, sizeof(shown), "<%s>", fields[i].value);
        else if (i == focus)
            snprintf(shown, sizeof(shown), "%s_", fields[i].value);
        else
            snprintf(shown, sizeof(shown), "%s", fields[i].value);
        font4x6_draw(s, box.x + 3, y + 5, shown, DLG_TEXT);
    }

    /* OK / Cancel hints */
    int hy = 20 + n * ROW_H + 4;
    ksurf_fill(s, KRECT(8, hy, 110, 12), DLG_BTN);
    font4x6_draw(s, 12, hy + 3, "ENTER = OK", DLG_TEXT);
    ksurf_fill(s, KRECT(126, hy, 126, 12), DLG_BTN);
    font4x6_draw(s, 130, hy + 3, "ESC = CANCEL", DLG_TEXT);
}

bool dialog_run(const char *title, DialogField *fields, int n)
{
    int h = 20 + n * ROW_H + 22;

    /* Center on the mouse's display */
    int mx, my;
    SDL_GetGlobalMouseState(&mx, &my);
    SDL_Rect geo;
    int x = mx - DLG_W / 2, y = my - h / 2;
    int di = SDL_GetPointDisplayIndex(&(SDL_Point){mx, my});
    if (di >= 0 && SDL_GetDisplayUsableBounds(di, &geo) == 0) {
        x = KA_CLAMP(x, geo.x, geo.x + geo.w - DLG_W);
        y = KA_CLAMP(y, geo.y, geo.y + geo.h - h);
    }

    SDL_Window *win = SDL_CreateWindow(
        title, x, y, DLG_W, h,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
            SDL_WINDOW_SKIP_TASKBAR);
    if (!win)
        return false;
    SDL_RaiseWindow(win);
    SDL_StartTextInput();

    int focus = 0;
    bool ok = false, done = false;
    while (!done) {
        SDL_Event ev;
        if (!SDL_WaitEventTimeout(&ev, 33))
            goto redraw;
        switch (ev.type) {
        case SDL_QUIT:
            done = true;
            SDL_PushEvent(&ev);
            break;
        case SDL_KEYDOWN: {
            SDL_Keycode k = ev.key.keysym.sym;
            DialogField *f = &fields[focus];
            if (k == SDLK_ESCAPE) {
                done = true;
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                ok = true;
                done = true;
            } else if (k == SDLK_TAB || k == SDLK_DOWN) {
                focus = (focus + 1) % n;
            } else if (k == SDLK_UP) {
                focus = (focus + n - 1) % n;
            } else if (k == SDLK_BACKSPACE && !f->options) {
                size_t len = strlen(f->value);
                if (len > 0)
                    f->value[len - 1] = '\0';
            } else if ((k == SDLK_LEFT || k == SDLK_RIGHT) && f->options) {
                int count = 0;
                while (f->options[count])
                    count++;
                f->option_idx =
                    (f->option_idx + (k == SDLK_RIGHT ? 1 : count - 1)) %
                    count;
                snprintf(f->value, sizeof(f->value), "%s",
                         f->options[f->option_idx]);
            }
            break;
        }
        case SDL_TEXTINPUT: {
            DialogField *f = &fields[focus];
            if (!f->options) {
                size_t len = strlen(f->value);
                size_t add = strlen(ev.text.text);
                if (len + add < sizeof(f->value))
                    memcpy(f->value + len, ev.text.text, add + 1);
            }
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (ev.button.windowID != SDL_GetWindowID(win)) {
                done = true;
            } else {
                int row = (ev.button.y - 20) / ROW_H;
                if (row >= 0 && row < n)
                    focus = row;
                /* OK / Cancel hint rows */
                int hy = 20 + n * ROW_H + 4;
                if (ev.button.y >= hy && ev.button.y < hy + 12) {
                    ok = ev.button.x < 120;
                    done = true;
                }
            }
            break;
        default:
            break;
        }
    redraw:;
        SDL_Surface *surf = SDL_GetWindowSurface(win);
        if (surf) {
            render_dialog(surf, title, fields, n, focus);
            SDL_UpdateWindowSurface(win);
        }
    }

    SDL_StopTextInput();
    SDL_DestroyWindow(win);
    return ok;
}

double dialog_get_num(const DialogField *fields, int n, const char *name,
                      double dflt)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(fields[i].name, name) == 0) {
            char *end;
            double v = strtod(fields[i].value, &end);
            return end == fields[i].value ? dflt : v;
        }
    }
    return dflt;
}

const char *dialog_get_str(const DialogField *fields, int n,
                           const char *name, const char *dflt)
{
    for (int i = 0; i < n; i++)
        if (strcmp(fields[i].name, name) == 0)
            return fields[i].value;
    return dflt;
}
