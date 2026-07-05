/* Settings persistence (INI file at ~/.config/kilix-amp/kilix-amp.ini).
 * Port of nixamp lib/config.py; keys use "section/name" form like QSettings. */
#ifndef KA_CONFIG_H
#define KA_CONFIG_H

#include "common.h"

typedef struct Config Config;

/* Loads from the default path (or `path` override for tests; NULL = default). */
Config *config_open(const char *path);
void config_close(Config *cfg); /* syncs, then frees */

const char *config_get(Config *cfg, const char *key, const char *dflt);
int config_get_int(Config *cfg, const char *key, int dflt);
bool config_get_bool(Config *cfg, const char *key, bool dflt);
double config_get_float(Config *cfg, const char *key, double dflt);

void config_set(Config *cfg, const char *key, const char *value);
void config_set_int(Config *cfg, const char *key, int value);
void config_set_bool(Config *cfg, const char *key, bool value);
void config_set_float(Config *cfg, const char *key, double value);

bool config_sync(Config *cfg); /* write to disk */

/* Convenience accessors mirroring the Python properties. */
int  config_volume(Config *c);            /* default 80 */
void config_set_volume(Config *c, int v);
int  config_balance(Config *c);           /* default 0 */
void config_set_balance(Config *c, int v);
bool config_shuffle(Config *c);
void config_set_shuffle(Config *c, bool v);
int  config_repeat(Config *c);
void config_set_repeat(Config *c, int v);
bool config_eq_enabled(Config *c);
void config_set_eq_enabled(Config *c, bool v);
double config_eq_preamp(Config *c);
void config_set_eq_preamp(Config *c, double v);
void config_get_eq_bands(Config *c, float out[10]);
void config_set_eq_bands(Config *c, const float bands[10]);
const char *config_skin_path(Config *c); /* "" if unset */
void config_set_skin_path(Config *c, const char *v);
bool config_double_size(Config *c);
void config_set_double_size(Config *c, bool v);
/* UI scale factor; falls back to double_size (2.0) then 1.0 when unset. */
double config_scale(Config *c);
void config_set_scale(Config *c, double v);
/* Returns false if no saved position. */
bool config_get_window_pos(Config *c, const char *window, int *x, int *y);
void config_set_window_pos(Config *c, const char *window, int x, int y);
bool config_eq_visible(Config *c);   /* default false */
void config_set_eq_visible(Config *c, bool v);
bool config_pl_visible(Config *c);   /* default true */
void config_set_pl_visible(Config *c, bool v);
int  config_pl_width(Config *c);     /* default 275 */
void config_set_pl_width(Config *c, int v);
int  config_pl_height(Config *c);    /* default 232 */
void config_set_pl_height(Config *c, int v);
bool config_ed_visible(Config *c);   /* default false */
void config_set_ed_visible(Config *c, bool v);
int  config_ed_width(Config *c);     /* default 275 */
void config_set_ed_width(Config *c, int v);
int  config_ed_height(Config *c);    /* default 300 */
void config_set_ed_height(Config *c, int v);

#endif
