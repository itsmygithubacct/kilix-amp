# Third-party code in this repository

`kilix-amp` itself is MIT licensed — see [LICENSE](LICENSE). The following
vendored component is **not** covered by that grant and carries its own:

## `termixer/`

A vendored third-party Rust project.

- **Licence:** MIT
- **Copyright:** (c) 2026 Daniel Tompkins
- **Full text:** [`termixer/LICENSE`](termixer/LICENSE), which is retained
  verbatim and must be preserved in any redistribution.

Its MIT terms require the copyright notice and permission notice to be included
in all copies or substantial portions. Redistributing `kilix-amp` therefore
means redistributing `termixer/LICENSE` alongside it.

## Linked libraries

Built against SDL2, libsndfile, PulseAudio and Opus. These are linked, not
vendored, and are supplied by the distribution under their own terms.
