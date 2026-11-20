# EnCodec playback

Native EnCodec `.kenc` file and live playback, model admission, and its
integration checks. This text was moved from the README; the
[README](../README.md) is the short entry point.

## EnCodec

Local `.kenc` files can use the optional native `libkilix-encodec` decoder.
Build with `make ENCODEC=1` after installing its headers, shared library and
pkg-config metadata. Build that library with `ONNX=1 CONTENT=1` and the exact
`CONTENT_SOURCE` and `CONTENT_COMMIT` supplied by the application closure.
Its embedded catalog, admission helper and build receipt travel with the
library. `ENCODEC_CFLAGS` and `ENCODEC_LIBS` can select an explicit development
installation. The default build has no EnCodec runtime dependency
and reports an unavailable-model error for these files.

Normal file and live opens require the selected model to be installed and
admitted by the packaged Kilix content catalog (asset/v3) and a durable
kilix-license acceptance receipt. The installed tree must be exactly where
kilix-content's `Installer.asset_destination()` places it,
`<content root>/assets/<asset id>`, and match the catalog's manifest.
`KILIX_CONTENT_ROOT` selects the content storage root; otherwise Amp uses the
account's NSS home plus `.local/gpu_terminal/kilix/data/desktop-apps`. A host
that relocates Kilix storage must pass its resolved content root.
Receipts are only read, never written, from the directory
`kilix_license.receipt_store_root()` names: `$KILIX_LICENSE_RECEIPTS` if set,
otherwise `$GPU_TERMINAL_HOME/license-receipts`, otherwise
`$HOME/.local/gpu_terminal/license-receipts` (the NSS home when `HOME` is
unset). The admission helper receives exactly those three variables.
`XDG_STATE_HOME` does not select the receipt store, and receipts filed under
the earlier `$XDG_STATE_HOME/kilix-content/license-receipts/v1` layout are not
read; those models must be accepted again. A host that relocates receipt
storage must give Amp the same `KILIX_LICENSE_RECEIPTS` or `GPU_TERMINAL_HOME`
(and `HOME`) as the licence screen that filed the receipts. The store must be
the user's own directory with no group or other access.
`KILIX_ENCODEC_THREADS` accepts `1` or `2` (default).
Amp never installs or downloads a model. Missing receipts, undeclared assets,
or incompatible bytes produce an error. The helper verifies the full admitted
population, including its notices, then passes only the exact 9/4 native files
as sealed descriptors. Native compiled graph hashes and runtime checks still
apply. Admission repeats for every model load; graph-directory environment
variables do not authorize normal playback. Regular input audio files are
also copied into sealed private snapshots.

The shared service uses one asynchronous native EnCodec source adapter and the
existing preamp/EQ/volume/pan/device path. An owned same-executable worker loads
models and decodes bounded records over a private socketpair; the UI performs
only nonblocking IPC and owned-child reaping. Pause keeps bounded prefetch;
stop or a source change kills only that worker. Workers inherit no application
FDs beyond their private channel, stderr and an explicitly selected live input,
and die if their parent exits.
Each has a 2 GiB address-space ceiling, 512 MiB file-size ceiling and 64 FD
ceiling. At most eight unreaped workers can exist during rapid source changes.

The 24 kHz mono profile supports 3/6/12 kb/s; 48 kHz stereo supports
3/6/12/24 kb/s. Seek starts at the verified preceding one-second mono epoch or
47520-sample stereo boundary; stereo pre-roll is handled by the shared decoder.
Old queued PCM is discarded and playback resumes after the worker acknowledges
the actual boundary. Loading and queue starvation are displayed as `loading`
and `buffering`. Stereo playback buffers at least 1.25 seconds before starting
(or the entire remainder for a shorter file), with a two-second device-queue
target. The selected thread count is reported through protocol 2; functional
playback does not by itself assert hardware or listening qualification.

For a live 24 kHz mono stream, use `--encodec-stdin` or
`--encodec-socket /absolute/private/source.sock`, without a file playlist.
Both controllers use the native framed input described in
[LIVE-FORMAT.md](../LIVE-FORMAT.md). These are binary EnCodec inputs, not raw PCM,
local `.kenc` files or arbitrary network streams. Live playback shows elapsed
time and `LIVE`, has no finite duration or seek control, and reports recovery,
end and reconnect state. Stdin is single-use for the lifetime of the player.
A Unix source can be reopened explicitly after disconnect; playback never
reconnects or advances automatically. Live entries are omitted when saving a
file playlist.

For explicit native integration checks, build
`make ENCODEC=1 build-encodec/native_encodec`, then run
`python3 tests/run_encodec.py --test-binary build-encodec/native_encodec
--codec-command /path/to/kenc --mono-assets /path/to/mono
--stereo-assets /path/to/stereo --content-root /path/to/installed-content
--evidence-dir /path/to/new-evidence-directory`.
The receipt environment and embedded catalog must admit the selected profiles;
`--profile 24k` or `--profile 48k` narrows the run. Explicit
`--development-only` replaces `--content-root` for byte/path worker fixtures
and omits the installed shared-audio checks. It supplies no admission credit.
The test creates synthetic local WAVs at all seven profile/rate combinations,
checks exact worker/pre-DSP PCM against the shared decoder, and exercises seek,
pause, EOF, cancellation and unrelated-child preservation. `make ENCODEC=1 test`
also exercises malformed private worker replies using a separate test binary.
`tests/headless_encodec.py` checks the real player socket with explicit fixtures;
its dummy SDL output belongs only to that test process.
Build `make ENCODEC=1 build-encodec/native_live` and pass the mono asset
directory and a synthetic mono `.kenc` file with at least two seconds of audio
plus the content root to test live input, recovery and the shared audio path.
An explicit `--development-only` final argument instead omits the installed
shared-audio check. Run
`python3 tests/headless_live.py ./kilix-amp /path/to/mono.kenc
--evidence-dir /path/to/new-directory` for actual protocol-2 stdin and Unix
reconnect controls. `KILIX_CONTENT_ROOT` and the matching receipt environment
must select an admitted mono population for these actual-player checks.

`tests/headless_admission_stereo.py` exercises the actual player against an
installed asset/v3 content root and a kilix-license receipt store holding only
the 48 kHz receipt. `--content-source` must be the kilix-content checkout the
linked libkilix-encodec was built from; the install location and receipt name
come from its verified catalog. It checks normal playback, repeated admission
after receipt and file changes, wrong-receipt, notice and membership refusal,
and unadmitted mono file/live refusal despite valid legacy graph directories.
All mutations use private copies; the original installed population and
receipts must remain byte-identical. `--probe build-encodec/admission_probe.so`
records each admission result; with `--no-model-runtime` (a library built
without ONNX) an admitted case must show admission returning the sealed files
and the native loader then refusing, since both end in the same player error.
