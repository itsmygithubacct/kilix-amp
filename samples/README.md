# Sample tracks

A test track so the Media Player's Open dialog isn't empty on first use.
Plebian-OS's `apps/amp.py` copies it into `~/Music` when the player launches.

## ode-to-joy.ogg

Beethoven's "Ode to Joy" (Symphony No. 9, fourth movement) is public domain.
This audio is a synthesized rendering generated from scratch, not a recording
of a performer and not the output of a MIDI file, sample, or soundfont.

The exact 62/62-event project-authored melody transcription is
[`ode-to-joy-notes.json`](ode-to-joy-notes.json), SHA-256
`3eabca03014a8e2aa4b0dc7eaf174254dc3afb12269ed874e8b1cfc1a08667b7`.
Its metadata identifies the composition and source method. The project author
dedicates that transcription and the generated recording under **Creative
Commons CC0 1.0 Universal (`CC0-1.0`)**: to the extent possible under law, the
author waives all copyright and related or neighbouring rights. The legal code
is <https://creativecommons.org/publicdomain/zero/1.0/legalcode>.

[`ode-to-joy.provenance.json`](ode-to-joy.provenance.json) binds the input,
generator, output, exact invocation and 6/6 relevant tool identities. Reproduce
and byte-verify the 243323/243323-byte Ogg file with:

```sh
python3 tools/verify_sample.py
```
