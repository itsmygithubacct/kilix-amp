#ifndef KA_MIDI_H
#define KA_MIDI_H
#include <stdbool.h>
/* Called in an isolated renderer process; output is owned by the service. */
bool midi_render_wav(const char *input, const char *output, char **error);
#endif
