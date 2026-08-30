#!/usr/bin/env python3
"""Reproduce and byte-verify Kilix Amp's checked-in CC0 sample."""
from __future__ import annotations

import hashlib
import json
import platform
from pathlib import Path
import subprocess
import sys
import tempfile

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
PROVENANCE = ROOT / "samples/ode-to-joy.provenance.json"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def refuse(reason: str) -> None:
    raise SystemExit(f"F101_AMP_SAMPLE_REFUSE:{reason}")


def package_version(name: str) -> str:
    result = subprocess.run(
        ["dpkg-query", "-W", "-f=${Version}", name],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    return result.stdout


def main() -> int:
    record = json.loads(PROVENANCE.read_text(encoding="utf-8"))
    if record.get("format") != "kilix-amp-sample-provenance/v1":
        refuse("PROVENANCE_FORMAT")
    if record.get("license") != "CC0-1.0":
        refuse("LICENSE_NOT_CC0_1_0")

    input_record = record["input"]
    generator_record = record["generator"]
    output_record = record["output"]
    source = ROOT / input_record["path"]
    generator = ROOT / generator_record["path"]
    checked_output = ROOT / output_record["path"]
    for label, path, expected in (
        ("INPUT", source, input_record["sha256"]),
        ("GENERATOR", generator, generator_record["sha256"]),
        ("OUTPUT", checked_output, output_record["sha256"]),
    ):
        if not path.is_file() or digest(path) != expected:
            refuse(f"{label}_DIGEST")
    if checked_output.stat().st_size != output_record["bytes"]:
        refuse("OUTPUT_SIZE")

    score = json.loads(source.read_text(encoding="utf-8"))
    events = score.get("events")
    if score.get("license") != "CC0-1.0":
        refuse("INPUT_LICENSE_NOT_CC0_1_0")
    if not isinstance(events, list) or len(events) != input_record["events"]:
        refuse("EVENT_COUNT")

    tools = record["tools"]
    ffmpeg_line = subprocess.run(
        ["ffmpeg", "-version"], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout.splitlines()[0]
    observed_tools = {
        "python": platform.python_version(),
        "numpy": np.__version__,
        "ffmpeg": ffmpeg_line.split()[2],
        "ffmpeg_sha256": digest(Path("/usr/bin/ffmpeg")),
        "libvorbis0a": package_version("libvorbis0a"),
        "libvorbisenc2": package_version("libvorbisenc2"),
    }
    if observed_tools != tools:
        refuse("TOOL_IDENTITY")

    expected_invocation = [
        "python3", "tools/render_sample.py",
        "samples/ode-to-joy-notes.json", "samples/ode-to-joy.ogg",
        "Ode to Joy — Kilix Amp CC0 sample",
    ]
    if record.get("invocation") != expected_invocation:
        refuse("INVOCATION")

    with tempfile.TemporaryDirectory(prefix="kilix-amp-sample-") as temporary:
        reproduced = Path(temporary) / "ode-to-joy.ogg"
        subprocess.run(
            [sys.executable, str(generator), str(source), str(reproduced),
             expected_invocation[-1]],
            cwd=ROOT,
            check=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if digest(reproduced) != output_record["sha256"]:
            refuse("REPRODUCED_DIGEST")
        if reproduced.read_bytes() != checked_output.read_bytes():
            refuse("REPRODUCED_BYTES")

    print(
        "F101 Amp sample provenance: PASS_EXACT "
        "artifacts=3/3 tools=6/6 events=62/62 invocation=1/1 "
        "reproduction=1/1 cc0_1_0=2/2"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
