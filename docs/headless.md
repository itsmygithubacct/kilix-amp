# Headless mode and control protocol

The `--headless` backend and its Unix-socket control protocol. This text was
moved from the README; the [README](../README.md) is the short entry point.

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
owner-only (0600). A second backend on a live socket is refused rather than
fighting the first one for the audio device. A stale socket left by a killed
run is also preserved and refused: after verifying that no Kilix Amp process is
running, remove that exact socket explicitly and restart. This fail-closed rule
avoids deleting a same-user replacement in a pathname race.

Headless mode **reads** the settings file and never writes it, so it cannot
overwrite the window layout or volume a windowed session is still using.

### Control protocol

One JSON object per line in, one per line out. Every reply carries `protocol`.
Version 1 retains the existing file-player contract. Version 2 adds explicit
source opens, live and model state, typed errors and exact wire timestamps.
Clients first send a read-only version-2 `ping`; they can fall back to a
version-1 `ping` when an older backend refuses. See
[CONTROL-PROTOCOL.md](../CONTROL-PROTOCOL.md) for version negotiation, strict
framing and the complete version-2 fields.

```console
$ printf '{"cmd":"state","protocol":1}\n' | nc -U ~/.local/gpu_terminal/kilix/session/kilix-amp.sock
{"protocol":1,"ok":true,"state":"playing","title":"Public Domain - Ode to Joy","file":"/home/…/ode-to-joy.ogg","pos":12.678,"len":17.777,"index":0,"count":1,"volume":80,"shuffle":false,"repeat":0}
```

| Command | Fields | Effect |
|---|---|---|
| `ping` | | liveness; version 2 also reports capabilities |
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
| `open` (v2) | `source_type`, `path` | replace with an explicit file or live Unix source |

Every mutating command answers with the state it produced, so a front end
redraws in one round trip. Failures reply `"ok":false` with an `error` string;
an unknown command is an error, never a silent no-op. `state` reports
`loading` between accepting a track and the decoder opening it — opening a file
is deferred to the next tick so a reply never waits on the decoder.
