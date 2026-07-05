/* Modal parameter dialog (replaces the Qt EffectParamDialog): labeled text
 * fields and option cycles, edited with the keyboard.
 * Tab/Up/Down switch fields, Left/Right cycle combo options, Enter accepts,
 * Esc cancels. */
#ifndef KA_DIALOG_H
#define KA_DIALOG_H

#include "common.h"

#define DIALOG_MAX_FIELDS 8
#define DIALOG_VALUE_LEN 128

typedef struct {
    const char *name;
    const char *label;
    /* Text/number field: initial value as text; combo: unused. */
    char value[DIALOG_VALUE_LEN];
    /* Combo: NULL-terminated option list (value tracks the selection). */
    const char **options;
    int option_idx;
} DialogField;

DialogField dialog_field_text(const char *name, const char *label,
                              const char *initial);
DialogField dialog_field_num(const char *name, const char *label,
                             double initial);
DialogField dialog_field_combo(const char *name, const char *label,
                               const char **options);

/* Runs modal; returns true on OK. Values are read back from fields[]. */
bool dialog_run(const char *title, DialogField *fields, int n);

double dialog_get_num(const DialogField *fields, int n, const char *name,
                      double dflt);
const char *dialog_get_str(const DialogField *fields, int n,
                           const char *name, const char *dflt);

#endif
