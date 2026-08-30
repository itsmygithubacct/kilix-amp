#!/usr/bin/env python3
"""Render a checked-in CC0 note list to a sample track, from scratch.

The samples this player ships must be redistributable without conditions, and
that rules out the obvious shortcut. A recording carries the performer's rights
even when the composition is public domain, and rendering a MIDI through a
sampled soundfont — FluidR3, TimGM, any of them — embeds that soundfont's
recorded waveforms in the output, so the result inherits whatever licence the
soundfont carries. Neither can honestly be called CC0.

So the tone here is computed, not sampled: a struck-string model built from a
harmonic series with per-partial decay, a hammer-noise transient, and a touch
of detuning between two voices per note. Nothing in the output originates in
anyone else's file. The exact project-authored note list is checked in beside
the output and dedicated under CC0-1.0.

Usage:
    python3 tools/render_sample.py notes.json out.ogg ["Title"]
"""
from __future__ import annotations

import json
import math
import subprocess
import sys
import tempfile
import wave

import numpy as np

RATE = 44100
OGG_SERIAL = 0x4B415031  # "KAP1": Kilix Amp provenance format 1


# ── exact, reviewable score input ───────────────────────────────────────────

def read_note_list(path: str) -> tuple[list[tuple[float, float, int, int]], float]:
    """Return (start, duration, MIDI note, velocity) and the score end."""
    with open(path, encoding="utf-8") as source:
        score = json.load(source)

    if score.get("format") != "kilix-amp-note-list/v1":
        raise SystemExit(f"{path}: unsupported note-list format")
    if score.get("license") != "CC0-1.0":
        raise SystemExit(f"{path}: note list must declare CC0-1.0")

    tempo = score.get("tempo_bpm")
    gate = score.get("gate_ratio")
    velocity = score.get("velocity")
    events = score.get("events")
    if not isinstance(tempo, (int, float)) or not math.isfinite(tempo) or tempo <= 0:
        raise SystemExit(f"{path}: tempo_bpm must be finite and positive")
    if not isinstance(gate, (int, float)) or not math.isfinite(gate) or not 0 < gate <= 1:
        raise SystemExit(f"{path}: gate_ratio must be in (0, 1]")
    if not isinstance(velocity, int) or not 1 <= velocity <= 127:
        raise SystemExit(f"{path}: velocity must be an integer in [1, 127]")
    if not isinstance(events, list) or not events:
        raise SystemExit(f"{path}: events must be a non-empty list")

    seconds_per_beat = 60.0 / tempo
    cursor = 0.0
    notes: list[tuple[float, float, int, int]] = []
    for index, event in enumerate(events):
        if not isinstance(event, list) or len(event) != 2:
            raise SystemExit(f"{path}: event {index} must be [note, beats]")
        note, beats = event
        if not isinstance(note, int) or not 0 <= note <= 127:
            raise SystemExit(f"{path}: event {index} note must be in [0, 127]")
        if not isinstance(beats, (int, float)) or not math.isfinite(beats) or beats <= 0:
            raise SystemExit(f"{path}: event {index} beats must be finite and positive")
        span = beats * seconds_per_beat
        notes.append((cursor, span * gate, note, velocity))
        cursor += span
    return notes, cursor


# ── a struck string, computed rather than sampled ───────────────────────────

def voice(note: int, seconds: float, velocity: int) -> np.ndarray:
    """One piano-ish note: harmonic series, per-partial decay, hammer noise."""
    freq = 440.0 * 2.0 ** ((note - 69) / 12.0)
    # Let every note ring past its written length; a piano does, and cutting it
    # at note-off is what makes a synthesised score sound like a organ.
    length = seconds + 1.6
    count = int(length * RATE)
    t = np.arange(count) / RATE
    amplitude = (velocity / 127.0) ** 1.4

    out = np.zeros(count)
    partial = 1
    while partial <= 12 and freq * partial < RATE / 2:
        # Higher partials start quieter and die sooner — that ratio is most of
        # what separates a struck string from a sawtooth.
        gain = 1.0 / partial ** 1.7
        decay = np.exp(-t * (1.6 + 0.55 * partial + freq / 2200.0))
        # Slight inharmonicity, as in a real string under tension.
        stretched = freq * partial * (1.0 + 0.00045 * partial * partial)
        out += gain * decay * np.sin(2 * np.pi * stretched * t)
        partial += 1

    # Two voices a few cents apart keep a single note from sounding sterile.
    detune = np.sin(2 * np.pi * freq * 1.0008 * t) * np.exp(-t * 2.2) * 0.18
    out += detune

    hammer = np.random.default_rng(note).standard_normal(count)
    hammer *= np.exp(-t * 120.0) * 0.06
    out += hammer

    attack = np.minimum(1.0, t / 0.004)            # 4ms, no click
    release = np.minimum(1.0, np.maximum(0.0, (length - t) / 0.25))
    return out * attack * release * amplitude * 0.28


def render(notes, end: float) -> np.ndarray:
    total = int((end + 2.5) * RATE)
    left = np.zeros(total)
    right = np.zeros(total)
    for start, seconds, note, velocity in notes:
        wave_data = voice(note, seconds, velocity)
        at = int(start * RATE)
        stop = min(total, at + len(wave_data))
        chunk = wave_data[:stop - at]
        # Low notes sit slightly left, high notes slightly right, the way a
        # piano is heard from the player's seat.
        pan = 0.5 + (note - 60) / 190.0
        pan = min(0.78, max(0.22, pan))
        left[at:stop] += chunk * (1.0 - pan)
        right[at:stop] += chunk * pan
    peak = max(np.max(np.abs(left)), np.max(np.abs(right)), 1e-9)
    scale = 0.89 / peak
    stereo = np.empty(total * 2)
    stereo[0::2] = left * scale
    stereo[1::2] = right * scale
    return stereo


def _ogg_crc(page: bytearray) -> int:
    """Return the non-reflected CRC-32 required by the Ogg framing spec."""
    checksum = 0
    for byte in page:
        checksum ^= byte << 24
        for _ in range(8):
            checksum = ((checksum << 1) ^ 0x04C11DB7) & 0xFFFFFFFF \
                if checksum & 0x80000000 else (checksum << 1) & 0xFFFFFFFF
    return checksum


def normalize_ogg(path: str) -> None:
    """Replace FFmpeg's random stream serial and repair every page checksum."""
    with open(path, "rb") as source:
        data = bytearray(source.read())
    cursor = 0
    pages = 0
    while cursor < len(data):
        if data[cursor:cursor + 4] != b"OggS" or cursor + 27 > len(data):
            raise SystemExit(f"{path}: invalid Ogg page at byte {cursor}")
        segments = data[cursor + 26]
        table_end = cursor + 27 + segments
        if table_end > len(data):
            raise SystemExit(f"{path}: truncated Ogg segment table")
        page_end = table_end + sum(data[cursor + 27:table_end])
        if page_end > len(data):
            raise SystemExit(f"{path}: truncated Ogg page payload")
        page = data[cursor:page_end]
        page[14:18] = OGG_SERIAL.to_bytes(4, "little")
        page[22:26] = b"\0\0\0\0"
        page[22:26] = _ogg_crc(page).to_bytes(4, "little")
        data[cursor:page_end] = page
        cursor = page_end
        pages += 1
    if pages == 0:
        raise SystemExit(f"{path}: no Ogg pages")
    with open(path, "wb") as destination:
        destination.write(data)


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__.strip())
        return 2
    source, destination = argv[0], argv[1]
    notes, end = read_note_list(source)
    print(f"{len(notes)} notes, {end:.1f}s")
    samples = render(notes, end)
    pcm = (np.clip(samples, -1.0, 1.0) * 32767).astype("<i2")
    with tempfile.NamedTemporaryFile(suffix=".wav") as raw:
        with wave.open(raw.name, "wb") as handle:
            handle.setnchannels(2)
            handle.setsampwidth(2)
            handle.setframerate(RATE)
            handle.writeframes(pcm.tobytes())
        command = ["ffmpeg", "-loglevel", "error", "-y",
                   "-fflags", "+bitexact", "-i", raw.name,
                   "-map_metadata", "-1", "-c:a", "libvorbis",
                   "-qscale:a", "3", "-flags:a", "+bitexact"]
        if len(argv) > 2:
            command += ["-metadata", f"title={argv[2]}"]
        command.append(destination)
        subprocess.run(command, check=True)
    normalize_ogg(destination)
    print(f"wrote {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
