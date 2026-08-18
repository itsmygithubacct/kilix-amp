# Third-party notices

`kilix-amp` itself is MIT licensed; see [LICENSE](LICENSE). The dispositions
below separate the root grant from vendored code, project-authored
documentation, generated sample media and distribution-supplied libraries.

## Termixer 0.1.3

`termixer/` is a vendored third-party Rust project.

- **Upstream:** <https://github.com/l00sed/termixer>
- **Version:** 0.1.3
- **Imported commit:**
  `3ffc7df61b1ab2d0dfb6dcde0f6cea1443367fa1`
- **Imported tree:**
  `df2d7d41f804c61db781eebb42ef5594c26bd1b5`
- **Kilix import commit:**
  `359167b0752be71f163b2b0bb3a2c2e84f73c1ce`
- **Licence:** MIT
- **Copyright:** Copyright (c) 2026 Daniel Tompkins
- **Full text:** [`termixer/LICENSE`](termixer/LICENSE)

The imported `termixer/` file tree matched that upstream commit byte-for-byte,
excluding Git metadata. This commit identifies itself as the 0.1.3 release,
but upstream tag `v0.1.3` points to the distinct earlier commit
`35942ce6d663d989bc8661671bc855766d01e16c`; the tag is therefore not used as
the source identity. The imported commit and tree above are authoritative.

Kilix carries two text modifications. `config/mpv.conf` selects terminal video
outputs (`kitty,sixel,tct`) instead of the window-opening `gpu` output.
`synthdefs/mixerChannel.scd` no longer contains the upstream author's absolute
workstation path; it writes through SuperCollider's configured SynthDef
directory and removes a duplicated trailing block. The two checked-in
`.scsyndef` payloads are retained unchanged. No Rust source, DSP or compiled
payload byte differs from the imported source.

Termixer's MIT copyright and permission notice must accompany every copy or
substantial portion of that component. Its Cargo manifest and lock refer to
Rust dependencies; dependency source is not vendored in this repository.

## Project screenshot

`docs/screenshot.png` has SHA-256
`43057c777cc51b943edfa5081b9a0bf4b027acb5e9def8fdae171c06fcef5287`.
It was added by the project author in commit
`77c8796f641777ea8d35fbe287429f8c717badd8` and contains the Kilix Amp user
interface plus plain track metadata. It embeds no album artwork, recording,
font file or other third-party binary. It is project documentation covered by
the root MIT grant, not a separately licensed third-party asset.

## Generated sample recording

`samples/ode-to-joy.ogg` has SHA-256
`e4bf0814fe81227ca0c7f7dc5272fdc3dc407ae880062fda0ef54cdc4ea05e25`.
Beethoven's composition is public domain. The recording is a project-generated
additive-synthesis rendering made from scratch without a performer, sample or
soundfont and is dedicated to the public domain under CC0. Its
provenance and generator are retained in [`samples/README.md`](samples/README.md)
and [`tools/render_sample.py`](tools/render_sample.py).

## Distribution-supplied libraries

The C application builds against SDL2, SDL2_image, libsndfile, zlib and
FluidSynth. They are linked from the operating-system dependency closure, not
vendored here, and retain their own package licences and notices. The Termixer
source has its separate Cargo dependency closure; no Termixer dependency source
is copied into this repository.
