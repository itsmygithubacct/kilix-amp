#include "midi.h"
#include "common.h"
#include <fluidsynth.h>
#include <stdlib.h>

#define MIDI_RENDER_RATE 44100

static char *find_soundfont(void)
{
    const char *env = getenv("KILIX_AMP_SOUNDFONT");
    if (env && *env && ka_is_file(env))
        return ka_strdup(env);

    static const char *paths[] = {
        "/usr/share/sounds/sf2/FluidR3_GM.sf2",
        "/usr/share/sounds/sf2/TimGM6mb.sf2",
        "/usr/share/sounds/sf2/default-GM.sf2",
        "/usr/share/soundfonts/FluidR3_GM.sf2",
        "/usr/share/soundfonts/TimGM6mb.sf2",
        "/usr/share/soundfonts/default-GM.sf2",
    };
    for (size_t i = 0; i < KA_LEN(paths); i++)
        if (ka_is_file(paths[i]))
            return ka_strdup(paths[i]);
    return NULL;
}

bool midi_render_wav(const char *midi_path, const char *out,
                               char **err)
{
    *err = NULL;

    char *sf2 = find_soundfont();
    if (!sf2) {
        *err = ka_strdup("No SoundFont found for MIDI playback");
        return false;
    }

    fluid_settings_t *settings = new_fluid_settings();
    fluid_synth_t *synth = NULL;
    fluid_player_t *player = NULL;
    fluid_file_renderer_t *renderer = NULL;
    bool ok = false;

    if (!settings) {
        *err = ka_strdup("Cannot initialize FluidSynth settings");
        goto done;
    }

    fluid_settings_setnum(settings, "synth.sample-rate", MIDI_RENDER_RATE);
    fluid_settings_setnum(settings, "synth.gain", 0.55);
    fluid_settings_setint(settings, "audio.period-size", 1024);
    fluid_settings_setstr(settings, "audio.file.name", out);
    fluid_settings_setstr(settings, "audio.file.type", "wav");
    fluid_settings_setstr(settings, "audio.file.format", "float");

    synth = new_fluid_synth(settings);
    if (!synth) {
        *err = ka_strdup("Cannot initialize FluidSynth synth");
        goto done;
    }
    if (fluid_synth_sfload(synth, sf2, 1) == FLUID_FAILED) {
        *err = ka_asprintf("Cannot load SoundFont: %s", sf2);
        goto done;
    }

    player = new_fluid_player(synth);
    if (!player || fluid_player_add(player, midi_path) == FLUID_FAILED) {
        *err = ka_asprintf("Cannot load MIDI: %s", ka_basename(midi_path));
        goto done;
    }

    renderer = new_fluid_file_renderer(synth);
    if (!renderer) {
        *err = ka_strdup("Cannot initialize FluidSynth file renderer");
        goto done;
    }

    if (fluid_player_play(player) == FLUID_FAILED) {
        *err = ka_strdup("Cannot start MIDI renderer");
        goto done;
    }

    int blocks = 0;
    int max_blocks = MIDI_RENDER_RATE * 60 * 60 / 1024; /* 1 hour guard. */
    while (fluid_player_get_status(player) == FLUID_PLAYER_PLAYING) {
        if (fluid_file_renderer_process_block(renderer) == FLUID_FAILED) {
            *err = ka_strdup("MIDI render failed");
            goto done;
        }
        if (++blocks > max_blocks) {
            *err = ka_strdup("MIDI render exceeded one hour");
            goto done;
        }
    }
    ok = true;

done:
    if (player) {
        fluid_player_stop(player);
        fluid_player_join(player);
    }
    if (renderer)
        delete_fluid_file_renderer(renderer);
    if (player)
        delete_fluid_player(player);
    if (synth)
        delete_fluid_synth(synth);
    if (settings)
        delete_fluid_settings(settings);
    free(sf2);

    return ok;
}
