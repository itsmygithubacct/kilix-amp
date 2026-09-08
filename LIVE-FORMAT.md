# Live EnCodec input

`--encodec-stdin` and `--encodec-socket PATH` consume the same binary stream in
windowed and headless Amp. They require a native EnCodec build and the verified
24 kHz model selected through `KILIX_ENCODEC_24KHZ_DIR`. The selected profile is
mono, 24 kHz, 3/6/12 kb/s (4/8/16 codebooks). Stereo remains a local-file format.

Start with the canonical 64-byte header produced by
`kenc_file_header_write` for `kenc_file_info {1, codebooks, KENC_FILE_LIVE, 0}`.
It has zero duration, record and index counts, a 64-byte data offset and the
standard 25-packet mono reset interval. Amp validates the complete header
through the shared library. A regular `.kenc` header is not a live header.

Each following record is a four-byte little-endian unsigned length followed
by one complete, opaque, codec-authored KMA2 packet. Length is 1 through
`KENC_MAX_PACKET_BYTES` (160); larger values are refused before allocation.
A zero length explicitly ends the stream without a payload. Physical writes
may split the prefix or packet arbitrarily. Bare transport EOF is a disconnect;
EOF inside a prefix or packet is truncation, not a clean end.

The shared library validates packet serialization and the selected profile,
then owns epoch/PTS/reset continuity and decoding. Amp never manufactures
KMA2 from ordinary PCM or changes packet fields. A valid discontinuity puts
the source into recovery: no PCM is emitted until a valid newer reset. At most
50 rejected packets and five seconds of recovery are allowed. Malformed
packets are fatal errors, not recoverable loss. The header and each complete
length-plus-payload record have separate five-second absolute read deadlines;
trickling bytes cannot extend a record's deadline. A connection may emit at
most 24 hours of decoded samples before requiring a new explicit open.

## Input ownership and playback

Stdin may be a pipe, regular file or stream socket. The worker duplicates the
explicit input; it does not change the caller's descriptor flags. For a regular
file, it reads from the caller's initial offset using a separate open-file
description, preserving the caller's offset. Other descriptor types refuse.
Stdin is single-use within a player process; start a new process for new stdin.

A Unix endpoint must be an absolute path shorter than 108 bytes, without dot,
dot-dot or empty components. Symlink traversal is refused. Ancestors must be
owned by root or this user and not writable by others, except a root-owned
sticky directory such as `/tmp`. The immediate parent must be owned by this
user with no group/other access. The endpoint must be a mode-0600 socket owned
by this user, and the connected peer must have the same UID. The worker pins
the parent directory and checks endpoint identity across connection. It never
deletes or chmods the endpoint or its ancestors.

The decoder runs in Amp's bounded owned worker, feeding the same float PCM,
preamp, EQ, volume, pan and SDL output as local files. The UI remains
nonblocking. Pausing preserves bounded transport and PCM queues; backpressure
can stop reading more records until playback resumes. It is not an end-to-end
latency guarantee. Stop or source replacement tears down only that worker.

Live sources have elapsed time and unknown duration, with seeking disabled.
Normal end drains queued PCM. A failure is latched and stops playback without
automatically reconnecting, advancing or repeating the playlist. Reopen a Unix
source explicitly to reconnect. File playlist saves omit ephemeral live
entries. Protocol 2 exposes live, recovery, end, model readiness and exact wire
timestamps; these functional fields do not certify performance or listening
quality.
