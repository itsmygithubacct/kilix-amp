# kilix-amp - Winamp Clone for Linux, in C

A pixel-faithful recreation of the classic Winamp 2.x music player, written
in C11 with SDL2. This is a full rewrite of the Python/Qt "nixamp" project
with the same feature set and window-for-window architecture.

![kilix-amp playing Raleigh Soliloquy Pt. I](docs/screenshot.png)

## Features

- **Classic Winamp 2.x UI** - Bitmap-based skin rendering with all the classic elements
- **4-window layout** - Main player, Equalizer, Playlist, and waveform Editor
- **Skin support** - Load .wsz skin files or skin directories (in-house ZIP reader)
- **10-band EQ** - In-house biquad filter chain with presets (Rock, Pop, Classical, ...)
- **Spectrum analyzer** - 75-bar display with peak dots, oscilloscope mode (in-house FFT)
- **Playlist** - M3U/PLS support, drag-and-drop, shuffle, repeat, sorting
- **Editor** - waveform view with 16 effects + 5 generators, selection, undo/redo
- **Window docking** - Snap windows together like the original
- **Keyboard shortcuts** - Z/X/C/V/B transport, all Winamp defaults
- **UI scaling** - integer 1x-4x scaling for HiDPI displays
- **Headless mode** - `--headless` serves a control socket, so a text front end
  drives the same decoder instead of reimplementing playback

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

Audio decoding goes through libsndfile for WAV, FLAC, Ogg/Vorbis, Opus, MP3
(libsndfile >= 1.1), AIFF and friends. MIDI files (`.mid`/`.midi`) are
rendered through FluidSynth using a GM SoundFont, then played through the same
SDL/EQ/volume pipeline as decoded audio. Set `KILIX_AMP_SOUNDFONT=/path/to.sf2`
to override the default SoundFont search.

Formats libsndfile cannot open (m4a/aac/wma) are skipped with an error title,
and the playlist auto-skips to the next track.

## Building

```bash
make            # release build -> ./kilix-amp
make debug      # ASan/UBSan debug build
make test       # build + run the unit test suite
```

## Usage

```bash
./kilix-amp                          # empty player
./kilix-amp track1.mp3 track2.flac   # play files
./kilix-amp /path/to/music/          # play a directory
./kilix-amp --skin path/to/skin.wsz  # custom skin
./kilix-amp --scale 3                # UI scale 1-4 (--double-size = --scale 2)
```

The UI scale can also be changed at runtime from the right-click menu
(SIZE section) or by pressing `D` (cycles 1x-4x); it persists across runs.

On first start a default skin is generated at
`${XDG_DATA_HOME:-~/.local/share}/kilix-amp/skins/default`. Settings are saved
to `${XDG_CONFIG_HOME:-~/.config}/kilix-amp/kilix-amp.ini` (the same INI
layout as nixamp's config). This lets desktop hosts give Kilix-Amp isolated,
persistent storage without changing `HOME`.

## Headless mode

`--headless` runs the player with no windows, no skin and no video subsystem —
only the decoder, the EQ chain and the playlist, driven over a Unix socket:

```bash
./kilix-amp --headless ~/Music/            # serve the default socket path
./kilix-amp --socket /tmp/amp.sock track.flac   # explicit path (implies --headless)
```

The point is that there is **one** playback implementation. A text front end
(`kilix-music` in kilix-tui-utils is the first) is a client of this socket
rather than a second decoder that drifts from this one.

The socket path is `$KILIX_AMP_SOCKET`, else `$XDG_RUNTIME_DIR/kilix-amp.sock`,
else `~/.local/gpu_terminal/kilix/session/kilix-amp.sock`. It is created
owner-only (0600). A socket left behind by a killed run is cleared on startup;
a second backend on a live socket is refused rather than fighting the first one
for the audio device.

Headless mode **reads** the settings file and never writes it, so it cannot
overwrite the window layout or volume a windowed session is still using.

### Control protocol

One JSON object per line in, one per line out. Every reply carries `protocol`,
so a client speaking another version is told so instead of having its fields
guessed at — this is a versioned contract between two repositories, and
bumping it is a breaking change.

```console
$ printf '{"cmd":"state","protocol":1}\n' | nc -U ~/.local/gpu_terminal/kilix/session/kilix-amp.sock
{"protocol":1,"ok":true,"state":"playing","title":"Public Domain - Ode to Joy","file":"/home/…/ode-to-joy.ogg","pos":12.678,"len":17.777,"index":0,"count":1,"volume":80,"shuffle":false,"repeat":0}
```

| Command | Fields | Effect |
|---|---|---|
| `ping` | | liveness only |
| `state` | | current status (the reply shown above) |
| `playlist` | | `items` (paths), `index`, `count` |
| `play` | `index` (optional) | play that entry, else resume or start |
| `toggle` | | play/pause |
| `pause` / `stop` | | pause; stop and unload |
| `next` / `previous` | | honours shuffle and repeat |
| `seek` | `pos` (seconds) | absolute seek |
| `volume` | `level` (0-100) | set volume |
| `add` | `path` (file or directory) | append to the playlist |
| `clear` | | stop and empty the playlist |
| `shuffle` | `on` (optional bool) | set, or toggle when omitted |
| `repeat` | `mode` (optional 0/1/2) | set, or cycle when omitted |
| `quit` | | shut the backend down |

Every mutating command answers with the state it produced, so a front end
redraws in one round trip. Failures reply `"ok":false` with an `error` string;
an unknown command is an error, never a silent no-op. `state` reports
`loading` between accepting a track and the decoder opening it — opening a file
is deferred to the next tick so a reply never waits on the decoder.

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Z | Previous track |
| X | Play |
| C | Pause |
| V | Stop |
| B | Next track |
| L | Open file |
| Left/Right | Seek -/+5 seconds |
| Up/Down | Volume up/down |
| S | Toggle shuffle |
| R | Toggle repeat |
| D | Cycle UI scale (1x-4x) |
| Alt+G | Toggle equalizer |
| Alt+E | Toggle playlist |
| Alt+D | Toggle editor |
| Ctrl+Z/Y/A | Editor: undo / redo / select all |

## Architecture

| Module | Purpose |
|--------|---------|
| `src/audio.c` | playback engine: libsndfile decode or FluidSynth MIDI render -> preamp/EQ/volume/pan -> SDL queued audio |
| `src/headless.c` | `--headless`: the engine and playlist wired to the control socket, no windows |
| `src/control.c` | non-blocking AF_UNIX server; line framing, so a quiet client cannot stall decoding |
| `src/json.c` | in-house JSON for the protocol: single-line writer, flat-object reader |
| `src/dsp.c` | in-house radix-2 FFT + biquad peaking filters |
| `src/effects.c` | 21 editor effects/generators (scipy replaced in-house) |
| `src/audio_data.c` | editor buffer with selection + undo/redo |
| `src/zip.c` | minimal ZIP reader (stored + deflate via zlib) for .wsz |
| `src/skin.c` | skin loader: BMP/PNG sheets, viscolor.txt, pledit.txt |
| `src/skin_default.c` | procedural default-skin generator |
| `src/font4x6.c` | in-house 4x6 pixel font (also baked into text.bmp) |
| `src/render.c` | software-surface compositing + present at 1x/2x |
| `src/widgets.c` | bitmap button/toggle/slider primitives + hit dispatch |
| `src/spectrum.c` | 75-bar analyzer + oscilloscope |
| `src/win_main.c` | main 275x116 player window |
| `src/win_eq.c` | 10-band equalizer window |
| `src/win_playlist.c` | resizable playlist editor |
| `src/win_editor.c` | resizable waveform editor |
| `src/dock.c` | window snapping/docking |
| `src/menu.c` / `src/dialog.c` | in-house popup menus and modal param dialogs |
| `src/filedialog.c` | zenity-backed file dialogs (text-prompt fallback) |
| `src/playlist.c` | playlist model with M3U/PLS |
| `src/config.c` | INI settings persistence |
| `src/main.c` | component wiring, hotkeys, main loop |

## Differences from the Python original

- **No system tray** - the original used Qt's tray API; no lightweight
  C equivalent exists without a GTK/appindicator dependency.
- **Menus are flattened** - Qt cascading submenus become section headers
  in a single popup.
- **File dialogs use zenity** when installed, otherwise a text-entry path
  prompt.
- **Decoding is libsndfile-based** instead of GStreamer, with native
  FluidSynth rendering for MIDI, so codec coverage differs slightly
  (no m4a/wma).
- **Pitch shift uses linear resampling** instead of scipy's FFT resampler;
  audibly equivalent for the +-12 semitone UI range.
- Everything else - window layout, skin coordinates, EQ curve math,
  playlist semantics, effect algorithms, config keys - is a direct port.

## Running Tests

```bash
make test
```

The suite (~5000 checks) covers the playlist model, M3U/PLS parsing, config
persistence, ZIP/skin loading including zip-bomb rejection, the default-skin
generator, FFT/biquad DSP, spectrum physics, all 21 effects/generators, the
editor buffer undo/redo model, and the widget primitives.
