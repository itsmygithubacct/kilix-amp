/* Window snap/dock behavior - snap windows together when edges are close.
 * Port of nixamp lib/docking.py. */
#ifndef KA_DOCK_H
#define KA_DOCK_H

#include "kwindow.h"

#define DOCK_MAX_WINDOWS 8

typedef struct {
    const char *names[DOCK_MAX_WINDOWS];
    KWindow *windows[DOCK_MAX_WINDOWS];
    int count;

    int dragging; /* index, or -1 */
    /* Offsets of windows docked to the dragged one (move together). */
    bool docked[DOCK_MAX_WINDOWS];
    int off_x[DOCK_MAX_WINDOWS], off_y[DOCK_MAX_WINDOWS];
} DockManager;

void dock_init(DockManager *dm);
void dock_register(DockManager *dm, const char *name, KWindow *win);
void dock_start_drag(DockManager *dm, const char *name);
/* Move the dragged window to (x, y) with snapping; docked windows follow. */
void dock_drag_move(DockManager *dm, const char *name, int x, int y);
void dock_end_drag(DockManager *dm);
/* Position windows in default Winamp layout (stacked). */
void dock_position_default(DockManager *dm);

#endif
