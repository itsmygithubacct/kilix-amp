# Build, dependencies and playback

Build dependencies, the mpv playback path, the shared Amp service, and the
test suite. This text was moved from the README; the [README](../README.md) is
the short entry point.

## Dependencies

Runtime/build libraries (Debian package names):

```bash
sudo apt install build-essential libsdl2-dev libsdl2-image-dev \
    libsndfile1-dev zlib1g-dev libfluidsynth-dev fluidsynth \
    fluid-soundfont-gm
# optional, for native file-open dialogs:
sudo apt install zenity
```

Or run:

```bash
./install-deps.sh
```

General music playback uses the installed `kilix-mpv` executable, including
MP3, FLAC, Ogg/Opus, AAC/M4A and WAV. MIDI is rendered by a cancellable
FluidSynth worker and then played through mpv. Set
`KILIX_AMP_SOUNDFONT=/path/to.sf2` to select a SoundFont. libsndfile remains a
build dependency for the audio editor and the native engine.

The GUI and `kilix-music` attach to one per-user Amp service. It owns the
playlist, playback position, volume, balance, ten-band EQ and repeat/shuffle
settings. Opening a second controller preserves the session; closing a
controller leaves playback running. Stop playback with Stop, or terminate the
service explicitly with the `quit` control command. Protocol 2 and the
`shared_playback` capability describe this lifecycle.

mpv's private JSON IPC controls general playback. The separately built
`libkilix-meter.so` passes audio unchanged and supplies spectrum samples from
the actual filtered output. `make all` builds both files; keep them together.
`make install PREFIX="$HOME/.local"` installs the pair and their license files.
`make runtime-check` verifies the installed mpv's decoder, lavfi/EQ/pan/LADSPA
filters and the companion library using generated silence and no audio device.
The 0.2.2 catalog runs this check as part of its selected build. `KILIX_MPV`
may select an explicit executable; `KILIX_AMP_AUDIO_OUTPUT=null` is for tests.

## Building

```bash
make            # release build -> ./kilix-amp
make debug      # ASan/UBSan debug build
make test       # build + run the unit test suite
```

## Running Tests

```bash
make test
```

The suite (~5000 checks) covers the playlist model, M3U/PLS parsing, config
persistence, ZIP/skin loading including zip-bomb rejection, the default-skin
generator, FFT/biquad DSP, spectrum physics, all 21 effects/generators, the
editor buffer undo/redo model, and the widget primitives.
