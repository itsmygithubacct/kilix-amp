/* In-house popup menu (replaces QMenu): a modal borderless window drawn
 * with the 4x6 font. Submenus are flattened under header rows. */
#ifndef KA_MENU_H
#define KA_MENU_H

#include "common.h"

typedef struct {
    const char *label; /* NULL = separator */
    int id;            /* returned on selection; < 0 = non-selectable header */
} MenuItem;

#define MENU_SEPARATOR ((MenuItem){NULL, -1})
#define MENU_HEADER(l) ((MenuItem){(l), -1})

/* Show a modal popup at global (x, y); returns the chosen item id or -1 if
 * dismissed. Runs a nested event loop until resolved. */
int menu_show(int x, int y, const MenuItem *items, int n);

#endif
