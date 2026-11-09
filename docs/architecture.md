# Architecture

Source modules, and the differences from the Python original. This text was
moved from the README; the [README](../README.md) is the short entry point.

## Architecture

| Module | Purpose |
|--------|---------|
| `src/audio.c` | selects exactly one output engine: mpv for general media, native EnCodec for file/live sources |
| `src/mpv_audio.c`, `meter/meter.c` | managed mpv IPC, filters, cancellable MIDI rendering and actual-output spectrum |
| `src/native_audio.c` | retained EnCodec admission, decoder, pre-DSP diagnostics and SDL output |
| `src/remote.c` | GUI controller of the shared protocol-2 service |
| `src/headless.c` | `--headless`: the engine and playlist wired to the control socket, no windows |
| `src/control.c` | non-blocking AF_UNIX server; line framing, so a quiet client cannot stall decoding |
| `src/json.c` | bounded strict request validation, typed field readers and single-line writer |
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
  FluidSynth rendering for MIDI; general playback formats come from kilix-mpv
  (no m4a/wma).
- **Pitch shift uses linear resampling** instead of scipy's FFT resampler;
  audibly equivalent for the +-12 semitone UI range.
- Everything else - window layout, skin coordinates, EQ curve math,
  playlist semantics, effect algorithms, config keys - is a direct port.
