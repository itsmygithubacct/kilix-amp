#!/usr/bin/env python3
"""check-deb.py DEB : kilix-amp installs the player and the meter library."""

import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message: str) -> None:
    print(f"check-deb: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: check-deb.py DEB")
    deb = Path(sys.argv[1])
    fields = subprocess.run(
        ["dpkg-deb", "-f", str(deb), "Package", "Version", "Architecture"],
        check=True, capture_output=True, text=True).stdout
    for needle in ("Package: kilix-amp\n", "Version: 0.20261029+packaging1\n"):
        if needle not in fields:
            fail(f"unexpected fields:\n{fields}")
    arch = ""
    for line in fields.splitlines():
        if line.startswith("Architecture: "):
            arch = line.split(": ", 1)[1]
    if arch not in {"amd64", "arm64"}:
        fail(f"unexpected architecture:\n{fields}")
    with tempfile.TemporaryDirectory(prefix="kilix-amp-deb.") as raw:
        root = Path(raw)
        subprocess.run(["dpkg-deb", "-x", str(deb), str(root)], check=True)
        command = root / "usr/bin/kilix-amp"
        library = root / "usr/lib/kilix-amp/libkilix-meter.so"
        player = root / "usr/lib/kilix-amp/kilix-amp"
        if not command.is_symlink() or not player.is_file() or not library.is_file():
            fail("player or meter missing")
        ran = subprocess.run([str(command), "--help"], check=False, capture_output=True, text=True)
        if ran.returncode != 0:
            fail(f"help failed ({ran.returncode}):\n{ran.stdout}\n{ran.stderr}")
        if "Usage: kilix-amp" not in ran.stdout:
            fail(f"help did not report usage:\n{ran.stdout}")
    print("check-deb: passed")


if __name__ == "__main__":
    main()
