# Kilix Amp control protocol

The headless backend serves newline-delimited UTF-8 JSON objects on its
owner-only Unix stream socket. Requests, including their terminating newline,
must fit 8192 bytes. Read until the complete reply newline; one `recv` call is
not a message boundary. Each request produces one reply with an integer
`protocol` and boolean `ok`. The server permits eight clients and drops a
connection after 15 seconds without read/write progress. Control polling and
audio playback share the same nonblocking main loop.

Requests must be complete JSON objects with unique, nonempty, unescaped ASCII
keys of at most 63 bytes. Nesting is limited to 16 and each object to 64 keys.
Malformed UTF-8, NUL, invalid surrogate pairs, duplicate keys, non-finite or
malformed numbers, trailing objects and truncated strings are refused before
command dispatch. All integer command fields must be JSON integers; booleans,
fractional numbers and numeric strings are not substitutes.

## Negotiation and compatibility

Send `{"cmd":"ping","protocol":2}` first. A version-2 backend replies with
`{"protocol":2,"ok":true,"max_protocol":2,"encodec":true,"live_sources":true}`
when built with native EnCodec support. A build without it reports both
capabilities false. Capabilities describe the build, not installed model
readiness or hardware qualification.

An older backend may return version 1 with an error. A client can then make a
fresh read-only `{"cmd":"ping","protocol":1}` request and use the documented
version-1 file commands. Do not retry a mutating request during negotiation.
Every subsequent request selects its version; accept only that version in its
reply. A missing request version selects 1. Unsupported or wrongly typed
versions are refused without dispatch, with a version-1 error and
`max_protocol:2`. Malformed framing also returns a version-1 error because
the request cannot safely establish a version.

Version 1 preserves the file-player fields and commands in the README. A
version-1 client connected while a live source is selected can only `ping`,
`stop` or `quit`; other commands receive an error requiring version 2. Its
`stop` reply is acknowledgement only. This prevents a legacy client from
interpreting unknown live duration as a finite file.

## Version-2 commands

All version-1 commands remain available. `open` replaces the playlist with one
explicit source and schedules loading on the next tick:

```json
{"cmd":"open","protocol":2,"source_type":"file","path":"/path/to/music.kenc"}
{"cmd":"open","protocol":2,"source_type":"encodec-unix","path":"/private/source.sock"}
```

`file` uses the existing playable-file rules. `encodec-unix` requires a bounded
absolute socket path and the endpoint checks in LIVE-FORMAT.md. Stdin must be
selected at process launch with `--encodec-stdin`; control requests cannot
replace the process's stdin. After a Unix disconnect, an explicit new `open`
starts a new connection. There is no automatic reconnect. `seek` refuses live,
unavailable or still-loading sources with `NOT_SEEKABLE`.

Optional `index`, `level` and `mode` fields must be integers when present;
optional `on` must be boolean. `seek.pos` must be a finite nonnegative number
within the player time range. Paths and command strings are never truncated
into different valid requests. Command failures retain `error` and add a
stable string `error_code` and boolean `error_recoverable`. The latter means
the control service remains usable; changing inputs or assets may be required.

## Version-2 state

The original `state`, `title`, `file`, `pos`, `index`, `count`, `volume`,
`shuffle` and `repeat` fields remain. `pos` is elapsed playback seconds.
`len` is a finite number only for a ready local file; it is JSON null for
live or unavailable/loading sources. Never invent a live end position or
seek percentage from it.

| Field | Meaning |
|---|---|
| `source_type` | `none`, `file`, `encodec-stdin` or `encodec-unix` |
| `codec` | `none`, `pcm` (ordinary decoded audio) or `encodec` |
| `profile` | EnCodec 1 for mono, 2 for stereo; zero when unknown |
| `sample_rate`, `channels`, `bitrate` | Verified source metadata; bitrate is bits/s |
| `threads` | Selected native inference threads, 1 or 2; zero before selection |
| `ready` | Source is loaded and available to the shared audio path |
| `model_ready` | Selected EnCodec source is ready; never inferred from build capability |
| `live`, `seekable`, `buffering` | Explicit playback/source controls |
| `ended` | The source has sent its clean end; queued audio may still be playing |
| `degraded` | Valid packet continuity was lost and a new reset is awaited |
| `reconnect_required` | Live source failed; a new explicit open is required |
| `wire_valid` | At least one packet was verified and decoded |
| `wire_pts_ms`, `wire_epoch` | Exact unsigned-64 decimal strings, or null before valid PCM |
| `source_error_code`, `source_error_message` | Latched source failure, separate from command acknowledgement |
| `source_error_recoverable` | A source error exists; the service can accept another request |

Wire timestamps describe the last verified decoded packet, independently of
elapsed playback or queued audio. Recovering packets do not advance verified
timestamps or fabricate PCM. Source metadata is reset when another source is
selected; pending opens do not borrow the previous source's metadata.

Source error codes are 0 (none), 1 (invalid arguments), 2 (model unavailable),
3 (runtime unavailable/failure), 4 (truncated input), 5 (protocol error),
6 (memory failure), 7 (live timeout), 8 (live disconnect), and 9 (unsafe or
unavailable live endpoint). Source error text is latched until another load.

Control paths are local user data. Frontends should check an owner-only socket
and same-UID peer before sending paths. The backend refuses to overwrite an
existing socket and removes only its own socket identity on clean shutdown.
